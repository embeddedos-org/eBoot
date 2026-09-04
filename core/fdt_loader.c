// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file fdt_loader.c
 * @brief FDT/DTB loader for eBoot — loads and passes device tree to kernel
 */

#include "eos_fdt_loader.h"
#include "eos_hal.h"
#include <stddef.h>
#include <string.h>

/* Big-endian to host conversion */
static uint32_t fdt32_to_cpu(uint32_t be)
{
    const uint8_t *p = (const uint8_t *)&be;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Safe unaligned 32-bit read + big-endian conversion */
static uint32_t fdt_read_u32(const uint8_t *ptr)
{
    uint32_t val;
    memcpy(&val, ptr, sizeof(val));
    return fdt32_to_cpu(val);
}

/* Header fields, read the same way the struct block is read. The blob may
 * be unaligned -- fuzz input, a buffer inside a larger message, a copy at an
 * odd offset -- and dereferencing a cast fdt_header_t* is a misaligned load
 * on strict-alignment targets, the same fault class this parser exists to
 * avoid. One rule for the whole blob: every multi-byte read goes through
 * memcpy. */
static uint32_t fdt_hdr_u32(const void *blob, size_t field_off)
{
    return fdt_read_u32((const uint8_t *)blob + field_off);
}

/* Does [off, off+len) sit inside a blob of totalsize bytes, without the
 * addition wrapping? Every offset in the header is attacker-controlled. */
static bool fdt_block_in_bounds(uint32_t off, uint32_t len, uint32_t totalsize)
{
    if (off < sizeof(fdt_header_t)) return false;
    if (len > totalsize) return false;
    return off <= totalsize - len;
}

int eos_fdt_validate(const void *fdt_blob, uint32_t avail)
{
    if (!fdt_blob) return -1;
    if (avail < sizeof(fdt_header_t)) return -6;
    if (fdt_hdr_u32(fdt_blob, offsetof(fdt_header_t, magic)) != FDT_MAGIC)
        return -2;
    if (fdt_hdr_u32(fdt_blob, offsetof(fdt_header_t, version)) < 16)
        return -3;

    /* Before anything else: the blob does not get to say how big it is. Every
     * bound below is relative to totalsize, so without this the checks only
     * prove the header is self-consistent — a 40-byte buffer claiming a 1 MiB
     * totalsize with agreeing block offsets passes all of them, and the walk
     * then runs a megabyte past the allocation. */
    if (fdt_hdr_u32(fdt_blob, offsetof(fdt_header_t, totalsize)) > avail)
        return -6;

    /* magic and version alone say nothing about where the blob claims its
     * blocks are. The struct and string offsets are read straight out of
     * flash and then used as pointers, so a blob that clears the two checks
     * above could still point them anywhere; eos_fdt_get_prop walked off the
     * end of a 40-byte allocation and took a bus fault. Reject the blob here
     * instead, because this is the gate every path goes through. */
    uint32_t totalsize = fdt_hdr_u32(fdt_blob, offsetof(fdt_header_t, totalsize));
    if (totalsize < sizeof(fdt_header_t)) return -6;
    if (!fdt_block_in_bounds(fdt_hdr_u32(fdt_blob, offsetof(fdt_header_t, off_dt_struct)),
                             fdt_hdr_u32(fdt_blob, offsetof(fdt_header_t, size_dt_struct)),
                             totalsize))
        return -6;
    if (!fdt_block_in_bounds(fdt_hdr_u32(fdt_blob, offsetof(fdt_header_t, off_dt_strings)),
                             fdt_hdr_u32(fdt_blob, offsetof(fdt_header_t, size_dt_strings)),
                             totalsize))
        return -6;
    return 0;
}


int eos_fdt_load(uint32_t flash_addr, void *dest, uint32_t max_size)
{
    if (!dest || max_size < sizeof(fdt_header_t)) return -1;

    /* Read header first to get total size */
    const void *src = (const void *)(uintptr_t)flash_addr;
    memcpy(dest, src, sizeof(fdt_header_t));

    /* Only the header has been copied so far, but max_size is what the caller
     * really owns, so that is the bound the blob has to satisfy. */
    int rc = eos_fdt_validate(dest, max_size);
    if (rc != 0) return rc;

    uint32_t total = fdt_hdr_u32(dest, offsetof(fdt_header_t, totalsize));
    if (total > max_size) return -4;

    /* Copy full DTB */
    memcpy(dest, src, total);
    return 0;
}

int eos_fdt_get_prop(const void *fdt, uint32_t fdt_len,
                     const char *node_path, const char *prop_name,
                     void *buf, uint32_t *buf_len)
{
    if (!fdt || !node_path || !prop_name || !buf || !buf_len) return -1;

    /* The blob is whatever was in flash. Every offset below comes out of its
     * header, so none of them can be trusted until validate() has bounded
     * them against both totalsize and the length the caller actually owns. */
    int vrc = eos_fdt_validate(fdt, fdt_len);
    if (vrc != 0) return vrc;

    const uint8_t *dt_struct = (const uint8_t *)fdt +
        fdt_hdr_u32(fdt, offsetof(fdt_header_t, off_dt_struct));
    const char *dt_strings = (const char *)fdt +
        fdt_hdr_u32(fdt, offsetof(fdt_header_t, off_dt_strings));
    uint32_t struct_size = fdt_hdr_u32(fdt, offsetof(fdt_header_t, size_dt_struct));
    uint32_t strings_size = fdt_hdr_u32(fdt, offsetof(fdt_header_t, size_dt_strings));

    /* Simple linear search through struct block */
    uint32_t offset = 0;
    int depth = 0;
    int target_depth = -1;
    bool in_target = false;

    /* Count slashes to determine target depth */
    int path_depth = 0;
    for (const char *p = node_path; *p; p++) {
        if (*p == '/') path_depth++;
    }
    if (path_depth == 0) path_depth = 1;

    /* offset < struct_size only guarantees one byte; every read below wants
     * four or more, so each is checked for the width it actually takes. */
    while (offset + 4 <= struct_size) {
        uint32_t tag = fdt_read_u32(dt_struct + offset);
        offset += 4;

        switch (tag) {
        case FDT_BEGIN_NODE: {
            /* strlen() here read until it happened to find a zero, which
             * for a name running to the end of the block is past it. */
            const char *name = (const char *)(dt_struct + offset);
            const void *nul = memchr(name, '\0', struct_size - offset);
            if (!nul) return -6;
            uint32_t name_len = (uint32_t)((const char *)nul - name) + 1;
            offset += (name_len + 3) & ~3U;
            if (offset > struct_size) return -6;
            depth++;

            /* Check if this node matches the target path */
            if (depth == path_depth) {
                /* Simple check: compare last component */
                const char *last_slash = strrchr(node_path, '/');
                const char *target_name = last_slash ? last_slash + 1 : node_path;
                if (strcmp(name, target_name) == 0 || target_name[0] == '\0') {
                    in_target = true;
                    target_depth = depth;
                }
            }
            break;
        }
        case FDT_END_NODE:
            if (in_target && depth == target_depth) in_target = false;
            /* An unbalanced blob would drive depth negative and let a later
             * BEGIN_NODE match path_depth at the wrong nesting level. */
            if (depth == 0) return -6;
            depth--;
            break;
        case FDT_PROP: {
            if (offset + 8 > struct_size) return -6;
            uint32_t len = fdt_read_u32(dt_struct + offset);
            uint32_t nameoff = fdt_read_u32(dt_struct + offset + 4);
            offset += 8;

            /* nameoff indexes the strings block; unchecked it named any
             * address, and strcmp then read from it. */
            if (nameoff >= strings_size) return -6;
            const char *pname = dt_strings + nameoff;
            if (!memchr(pname, '\0', strings_size - nameoff)) return -6;

            /* The value has to be inside the struct block before it is read:
             * copy_len was clamped to the caller's buffer, which bounded the
             * write but not the read, so an oversized len leaked whatever
             * followed the blob into buf. */
            if (len > struct_size - offset) return -6;

            if (in_target && strcmp(pname, prop_name) == 0) {
                /* Truncating and returning 0 told the caller it had the whole
                 * value. This path reads bootargs: a clipped string that
                 * reports success drops whatever sat at its end, and nothing
                 * distinguishes that from a short property. Report the full
                 * length so the caller can size a retry. */
                if (len > *buf_len) {
                    *buf_len = len;
                    return -7;
                }
                memcpy(buf, dt_struct + offset, len);
                *buf_len = len;
                return 0;
            }
            /* len is bounded above, so the pad cannot wrap. */
            offset += (len + 3) & ~3U;
            if (offset > struct_size) return -6;
            break;
        }
        case FDT_END:
            return -5; /* Property not found */
        case FDT_NOP:
            /* Legal padding; the spec allows it anywhere between tokens. */
            break;
        default:
            /* Refuse, do not resynchronise. Skipping an unknown tag and
             * treating whatever sits 4 bytes on as the next token meant a
             * struct block of arbitrary bytes parsed to completion. Every
             * read stayed bounded, so this was not memory-unsafe -- but a
             * parser in the TCB that walks garbage to a clean "not found"
             * is quietly accepting input it does not understand. */
            return -6;
        }
    }
    return -5;
}


void eos_fdt_pass_to_kernel(uint32_t dtb_addr)
{
    /* The DTB address is passed to the kernel via:
     * ARM32: r2 register
     * ARM64: x0 register
     * RISC-V: a1 register
     * This is handled by the boot jump code in rtos_boot.c */
    (void)dtb_addr;
}
