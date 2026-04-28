// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file fdt_loader.c
 * @brief FDT/DTB loader for eBoot — loads and passes device tree to kernel
 */

#include "eos_fdt_loader.h"
#include "eos_hal.h"
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

int eos_fdt_validate(const void *fdt_blob)
{
    if (!fdt_blob) return -1;
    const fdt_header_t *hdr = (const fdt_header_t *)fdt_blob;
    if (fdt32_to_cpu(hdr->magic) != FDT_MAGIC) return -2;
    if (fdt32_to_cpu(hdr->version) < 16) return -3;
    return 0;
}

int eos_fdt_load(uint32_t flash_addr, void *dest, uint32_t max_size)
{
    if (!dest || max_size < sizeof(fdt_header_t)) return -1;

    /* Read header first to get total size */
    const void *src = (const void *)(uintptr_t)flash_addr;
    memcpy(dest, src, sizeof(fdt_header_t));

    int rc = eos_fdt_validate(dest);
    if (rc != 0) return rc;

    const fdt_header_t *hdr = (const fdt_header_t *)dest;
    uint32_t total = fdt32_to_cpu(hdr->totalsize);
    if (total > max_size) return -4;

    /* Copy full DTB */
    memcpy(dest, src, total);
    return 0;
}

int eos_fdt_get_prop(const void *fdt, const char *node_path,
                      const char *prop_name, void *buf, uint32_t *buf_len)
{
    if (!fdt || !node_path || !prop_name || !buf || !buf_len) return -1;

    const fdt_header_t *hdr = (const fdt_header_t *)fdt;
    const uint8_t *dt_struct = (const uint8_t *)fdt + fdt32_to_cpu(hdr->off_dt_struct);
    const char *dt_strings = (const char *)fdt + fdt32_to_cpu(hdr->off_dt_strings);
    uint32_t struct_size = fdt32_to_cpu(hdr->size_dt_struct);

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

    while (offset < struct_size) {
        uint32_t tag = fdt_read_u32(dt_struct + offset);
        offset += 4;

        switch (tag) {
        case FDT_BEGIN_NODE: {
            const char *name = (const char *)(dt_struct + offset);
            uint32_t name_len = (uint32_t)strlen(name) + 1;
            offset += (name_len + 3) & ~3U;
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
            depth--;
            break;
        case FDT_PROP: {
            uint32_t len = fdt_read_u32(dt_struct + offset);
            uint32_t nameoff = fdt_read_u32(dt_struct + offset + 4);
            offset += 8;
            const char *pname = dt_strings + nameoff;

            if (in_target && strcmp(pname, prop_name) == 0) {
                uint32_t copy_len = len < *buf_len ? len : *buf_len;
                memcpy(buf, dt_struct + offset, copy_len);
                *buf_len = copy_len;
                return 0;
            }
            offset += (len + 3) & ~3U;
            break;
        }
        case FDT_END:
            return -5; /* Property not found */
        default:
            break;
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
