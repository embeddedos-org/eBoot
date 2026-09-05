// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file eos_fw_update.h
 * @brief Firmware update pipeline — stream-based image install
 *
 * Provides a chunk-based firmware update API that receives firmware
 * data in small pieces, verifies integrity, and installs to the
 * inactive slot. Designed for memory-constrained devices.
 *
 * Usage:
 *   eos_fw_update_ctx_t ctx;
 *   eos_fw_update_begin(&ctx, EOS_SLOT_B);
 *   while (data_available) {
 *       eos_fw_update_write(&ctx, chunk, chunk_len);
 *   }
 *   eos_fw_update_finalize(&ctx, EOS_UPGRADE_TEST);
 */

#ifndef EOS_FW_UPDATE_H
#define EOS_FW_UPDATE_H

#include "eos_types.h"
#include "eos_image.h"
#include "eos_crypto_boot.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Update State ---- */

typedef enum {
    EOS_FW_STATE_IDLE       = 0,
    EOS_FW_STATE_HEADER     = 1,
    EOS_FW_STATE_PAYLOAD    = 2,
    EOS_FW_STATE_TLV        = 3,
    EOS_FW_STATE_VERIFY     = 4,
    EOS_FW_STATE_COMPLETE   = 5,
    EOS_FW_STATE_ERROR      = 6,
} eos_fw_update_state_t;

typedef struct {
    eos_fw_update_state_t state;
    eos_slot_t target_slot;
    uint32_t target_addr;
    uint32_t slot_size;

    /* Header buffer (accumulated until full header received) */
    uint8_t hdr_buf[sizeof(eos_image_header_t)];
    uint32_t hdr_received;
    eos_image_header_t header;
    bool header_parsed;

    /* Streaming write state */
    uint32_t payload_written;
    uint32_t payload_total;
    uint32_t tlv_written;
    uint32_t tlv_total;
    uint32_t write_addr;

    /* Integrity tracking */
    eos_sha256_ctx_t sha_ctx;
    uint32_t crc_accum;

    /* Progress */
    uint32_t total_received;
    int last_error;
} eos_fw_update_ctx_t;

/* ---- Firmware Update API ---- */

/**
 * Begin a firmware update session. Erases the target slot.
 *
 * @param ctx   Update context (caller-allocated).
 * @param slot  Target slot (must not be the active slot).
 * @return EOS_OK on success.
 */
int eos_fw_update_begin(eos_fw_update_ctx_t *ctx, eos_slot_t slot);

/**
 * Write a chunk of firmware data. Handles header parsing, payload
 * streaming, and the authenticated TLV tail (hdr.tlv_len bytes after
 * the payload). Extra bytes beyond that container are rejected;
 * they are not discarded with EOS_OK.
 *
 * @param ctx   Update context.
 * @param data  Chunk of firmware data.
 * @param len   Length of chunk.
 * @return EOS_OK on success, negative on error.
 */
int eos_fw_update_write(eos_fw_update_ctx_t *ctx, const uint8_t *data, size_t len);

/**
 * Number of container bytes eos_fw_update_write() will still accept.
 *
 * A block-framed transport such as XMODEM has no container length of its
 * own and pads its final block; this is how it tells image bytes from
 * padding instead of pushing the padding into eos_fw_update_write(), which
 * rejects anything past the container.
 *
 * While the header is still arriving only the header remainder is known,
 * because image_size and tlv_len are fields inside it. Call this again
 * after each write rather than once per block.
 *
 * @param ctx   Update context.
 * @return Bytes still wanted, or 0 once the container is complete or the
 *         context is idle or failed.
 */
uint32_t eos_fw_update_bytes_wanted(const eos_fw_update_ctx_t *ctx);

/**
 * Finalize the update: verify integrity, update boot control,
 * and optionally mark for test boot.
 *
 * @param ctx   Update context.
 * @param mode  Upgrade mode (test or permanent).
 * @return EOS_OK on success.
 */
int eos_fw_update_finalize(eos_fw_update_ctx_t *ctx, eos_upgrade_mode_t mode);

/**
 * Abort an in-progress update and clean up.
 */
void eos_fw_update_abort(eos_fw_update_ctx_t *ctx);

/**
 * Get current update progress (0-100).
 */
uint8_t eos_fw_update_progress(const eos_fw_update_ctx_t *ctx);

/**
 * Get the current update state.
 */
eos_fw_update_state_t eos_fw_update_get_state(const eos_fw_update_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* EOS_FW_UPDATE_H */
