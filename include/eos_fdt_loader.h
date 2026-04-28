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

#define FDT_MAGIC           0xD00DFEEDU
#define FDT_BEGIN_NODE      0x00000001U
#define FDT_END_NODE        0x00000002U
#define FDT_PROP            0x00000003U
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

int eos_fdt_validate(const void *fdt_blob);
int eos_fdt_load(uint32_t flash_addr, void *dest, uint32_t max_size);
int eos_fdt_get_prop(const void *fdt, const char *node_path,
                      const char *prop_name, void *buf, uint32_t *buf_len);
void eos_fdt_pass_to_kernel(uint32_t dtb_addr);

#ifdef __cplusplus
}
#endif
#endif /* EOS_FDT_LOADER_H */
