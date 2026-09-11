// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file eos_boot_log.h
 * @brief Structured boot logging for eBootloader
 *
 * Provides a persistent, ring-buffer-based boot log stored in a
 * dedicated flash sector. Each boot cycle appends timestamped entries
 * that record slot selection, image validation, rollback events, and
 * firmware update status. The log survives resets and can be read
 * by application firmware via eos_fw_read_boot_log().
 */

#ifndef EOS_BOOT_LOG_H
#define EOS_BOOT_LOG_H

#include "eos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- Boot Log Configuration ---------------- */

#define EOS_BOOT_LOG_SECTOR_SIZE   4096
#define EOS_BOOT_LOG_ENTRY_SIZE    sizeof(eos_boot_log_entry_t)

/* ---------------- Boot Log API ---------------- */

/**
 * @brief Initialize the boot log subsystem.
 *
 * @param head  Ring-buffer write position recovered from the boot control
 *              block. Values >= EOS_BOOT_LOG_MAX wrap.
 *
 * Until this is called, eos_boot_log_append() is a no-op — stage0 must not
 * write log entries before the boot control block has been read.
 */
void eos_boot_log_init(uint32_t head);

/**
 * @brief Append a log entry at the current head and advance it.
 *
 * The entry is timestamped with eos_hal_get_tick_ms() and written straight to
 * the log sector. When the ring wraps, the oldest entry is overwritten.
 *
 * @param event   Event code (EOS_LOG_BOOT_START, EOS_LOG_ROLLBACK, ...).
 * @param slot    Associated slot (EOS_SLOT_A, EOS_SLOT_B, or EOS_SLOT_NONE).
 * @param detail  Event-specific detail (version, error code, ...).
 */
void eos_boot_log_append(uint32_t event, uint32_t slot, uint32_t detail);

/**
 * @brief Current ring-buffer write position.
 *
 * Persisted into the boot control block on handoff so the log survives a
 * reset. @return Head index in [0, EOS_BOOT_LOG_MAX).
 */
uint32_t eos_boot_log_get_head(void);

/**
 * @brief Read one log entry by ring index.
 *
 * @param index  Entry index in [0, EOS_BOOT_LOG_MAX).
 * @param out    Receives the entry. Untouched unless EOS_OK is returned.
 * @return EOS_OK on success, EOS_ERR_INVALID for a null @p out or an index
 *         past the end of the ring, EOS_ERR_GENERIC if the board has no ops,
 *         or the flash driver's error.
 */
int eos_boot_log_read(uint32_t index, eos_boot_log_entry_t *out);

/**
 * @brief Erase the log sector and reset the write position.
 * @return EOS_OK on success, otherwise the flash driver's error.
 */
int eos_boot_log_clear(void);

#ifdef __cplusplus
}
#endif
#endif /* EOS_BOOT_LOG_H */
