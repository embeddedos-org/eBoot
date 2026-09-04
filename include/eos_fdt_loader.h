// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file eos_fdt_loader.h
 * @brief Flattened Device Tree (FDT) loading for eBoot
 */

#ifndef EOS_FDT_LOADER_H
#define EOS_FDT_LOADER_H

#include "eos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Longest node path this parser resolves, in components. A caller cannot
 * act on return code -8 without knowing the limit, so it is declared here
 * rather than privately in fdt_loader.c. */
#define FDT_MAX_PATH_DEPTH  16

#define FDT_MAGIC           0xD00DFEEDU
#define FDT_BEGIN_NODE      0x00000001U
#define FDT_END_NODE        0x00000002U
#define FDT_PROP            0x00000003U
#define FDT_NOP             0x00000004U
#define FDT_END             0x00000009U

typedef struct {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
} fdt_header_t;

/**
 * Return codes. Every entry point below returns 0 on success and one of these
 * on failure; they are distinct so a caller can tell a programming error from
 * a malformed blob from a buffer that was too small.
 *
 *   -1  a required argument was NULL
 *   -2  the blob does not carry the FDT magic
 *   -3  the blob declares an unsupported version (< 16)
 *   -4  the blob does not fit the destination buffer
 *   -5  no such node or property
 *   -6  the blob is malformed: an offset or length escapes it
 *   -7  the property is larger than the caller's buffer (see below)
 *   -8  the node path is deeper than FDT_MAX_PATH_DEPTH components
 */

/**
 * Validate a blob.
 *
 * @param avail  bytes actually readable at @p fdt_blob.
 *
 * There is deliberately no length-free form. Every bound inside the header --
 * the struct and string block offsets and sizes -- is expressed relative to
 * the header's own totalsize field, which is attacker-controlled; checking
 * those against each other proves the blob is internally consistent and says
 * nothing about how many bytes are really mapped. An earlier revision kept a
 * one-argument wrapper that read totalsize and passed it as the bound, which
 * is an out-of-bounds read on a short buffer before any check has run. A
 * documented caller warrant is not a check, and an exported unsafe twin in a
 * TCB header is a future boot-path caller reintroducing the bug with no
 * compiler complaint.
 */
int eos_fdt_validate(const void *fdt_blob, uint32_t avail);

int eos_fdt_load(uint32_t flash_addr, void *dest, uint32_t max_size);

/**
 * Read a property.
 *
 * @param fdt_len  bytes actually readable at @p fdt.
 * @param buf_len  in: capacity of @p buf. out: bytes written on success, or
 *                 the property's full length when -7 is returned, so the
 *                 caller can size a retry.
 *
 * Returns -7 rather than truncating. A boot path reads bootargs through here,
 * and a silently clipped value that reports success loses whatever sat at the
 * end of the string.
 */
int eos_fdt_get_prop(const void *fdt, uint32_t fdt_len,
                     const char *node_path, const char *prop_name,
                     void *buf, uint32_t *buf_len);

void eos_fdt_pass_to_kernel(uint32_t dtb_addr);

#ifdef __cplusplus
}
#endif
#endif /* EOS_FDT_LOADER_H */
