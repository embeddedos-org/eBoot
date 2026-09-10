// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

#include "eos_image.h"

bool eos_image_fits_slot(const eos_image_header_t *hdr, uint32_t slot_size)
{
    if (!hdr)
        return false;

    if (hdr->hdr_size > slot_size)
        return false;

    uint32_t remaining = slot_size - hdr->hdr_size;

    if (hdr->image_size > remaining)
        return false;

    remaining -= hdr->image_size;

    if (hdr->tlv_len > remaining)
        return false;

    return true;
}
