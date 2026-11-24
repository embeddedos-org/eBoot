// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file eos_types.h
 * @brief Core type definitions for eBootloader
 *
 * Common types, constants, and return codes used across all
 * eBootloader modules.
 */

#ifndef EOS_TYPES_H
#define EOS_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- Return Codes ---------------- */

#define EOS_OK              0
#define EOS_ERR_GENERIC    -1
#define EOS_ERR_INVALID    -2
#define EOS_ERR_CRC        -3
#define EOS_ERR_SIGNATURE  -4
#define EOS_ERR_NO_IMAGE   -5
#define EOS_ERR_FLASH      -6
#define EOS_ERR_TIMEOUT    -7
#define EOS_ERR_BUSY       -8
#define EOS_ERR_AUTH       -9
#define EOS_ERR_VERSION    -10
#define EOS_ERR_FULL           -11
#define EOS_ERR_NOT_FOUND      -12
#define EOS_ERR_NOT_SUPPORTED  -13
#define EOS_ERR_ANTI_ROLLBACK  -14
#define EOS_ERR_REPLAY         -15
#define EOS_ERR_DECRYPT        -16
#define EOS_ERR_KEY            -17
#define EOS_ERR_DEBUG_LOCKED   -18
#define EOS_ERR_WRITE_PROTECTED -19

/* ---------------- Magic Numbers ---------------- */

#define EOS_BOOTCTL_MAGIC   0x45424354  /* "EBCT" */
#define EOS_IMG_MAGIC       0x454F5349  /* "EOSI" */
#define EOS_LOG_MAGIC       0x454C4F47  /* "ELOG" */

/* ---------------- Slot Identifiers ---------------- */

typedef enum {
    EOS_SLOT_A        = 0,
    EOS_SLOT_B        = 1,
    EOS_SLOT_RECOVERY = 2,
    EOS_SLOT_NONE     = 0xFF
} eos_slot_t;

/* ---------------- Upgrade Modes ---------------- */

typedef enum {
    EOS_UPGRADE_TEST,
    EOS_UPGRADE_PERMANENT
} eos_upgrade_mode_t;

/* ---------------- Boot Flags ---------------- */

#define EOS_FLAG_FORCE_RECOVERY   (1U << 0)
#define EOS_FLAG_TEST_BOOT        (1U << 1)
#define EOS_FLAG_CONFIRMED        (1U << 2)
#define EOS_FLAG_ROLLBACK         (1U << 3)
#define EOS_FLAG_FACTORY_RESET    (1U << 4)
#define EOS_FLAG_UPGRADE_PENDING  (1U << 5)

/* ---------------- Reset Reasons ---------------- */

typedef enum {
    EOS_RESET_POWER_ON    = 0,
    EOS_RESET_WATCHDOG    = 1,
    EOS_RESET_SOFTWARE    = 2,
    EOS_RESET_PIN         = 3,
    EOS_RESET_BROWNOUT    = 4,
    EOS_RESET_LOCKUP      = 5,
    EOS_RESET_UNKNOWN     = 0xFF
} eos_reset_reason_t;

/* ---------------- Image Flags ---------------- */

#define EOS_IMG_FLAG_ENCRYPTED    (1U << 0)
#define EOS_IMG_FLAG_COMPRESSED   (1U << 1)
#define EOS_IMG_FLAG_DEBUG        (1U << 2)

/* ---------------- Signature Types ---------------- */

typedef enum {
    EOS_SIG_NONE    = 0,
    EOS_SIG_CRC32   = 1,
    EOS_SIG_SHA256  = 2,
    EOS_SIG_ED25519 = 3,
    EOS_SIG_ECDSA   = 4
} eos_sig_type_t;

/* ---------------- Version Encoding ---------------- */

#define EOS_VERSION_MAKE(major, minor, patch) \
    (((uint32_t)(major) << 24) | ((uint32_t)(minor) << 16) | ((uint32_t)(patch) & 0xFFFF))

#define EOS_VERSION_MAJOR(v) (((v) >> 24) & 0xFF)
#define EOS_VERSION_MINOR(v) (((v) >> 16) & 0xFF)
#define EOS_VERSION_PATCH(v) ((v) & 0xFFFF)

/* ---------------- Slot State ---------------- */

typedef enum {
    EOS_SLOT_STATE_EMPTY     = 0,
    EOS_SLOT_STATE_VALID     = 1,
    EOS_SLOT_STATE_INVALID   = 2,
    EOS_SLOT_STATE_TESTING   = 3,
    EOS_SLOT_STATE_CONFIRMED = 4
} eos_slot_state_t;

/* ---------------- Boot Log Entry ---------------- */

typedef struct {
    uint32_t timestamp;
    uint32_t event;
    uint32_t slot;
    uint32_t detail;
} eos_boot_log_entry_t;

/* Boot log events */
#define EOS_LOG_BOOT_START      0x01
#define EOS_LOG_IMAGE_VALID     0x02
#define EOS_LOG_IMAGE_INVALID   0x03
#define EOS_LOG_SLOT_SELECTED   0x04
#define EOS_LOG_ROLLBACK        0x05
#define EOS_LOG_RECOVERY_ENTER  0x06
#define EOS_LOG_UPGRADE_START   0x07
#define EOS_LOG_UPGRADE_DONE    0x08
#define EOS_LOG_CONFIRM         0x09
#define EOS_LOG_FACTORY_RESET   0x0A
#define EOS_LOG_WATCHDOG_RESET  0x0B
#define EOS_LOG_BOOT_FAIL       0x0C
/* Recovery-session authentication. Emitted by core/recovery.c; decoded by
 * tools/uart_recovery.py, whose table tests/unit/test_boot_log_event_names.py
 * keeps in step with this list. */
#define EOS_LOG_AUTH_SUCCESS       0x20
#define EOS_LOG_AUTH_FAIL          0x21
#define EOS_LOG_AUTH_UNPROVISIONED 0x22
#define EOS_LOG_AUTH_NO_ENTROPY    0x23

/* Maximum constants */
#define EOS_MAX_BOOT_ATTEMPTS   3
#define EOS_BOOT_LOG_MAX        32
#define EOS_HASH_SIZE           32
#define EOS_SIG_MAX_SIZE        64

/* `detail` values stage-0 attaches to EOS_LOG_BOOT_FAIL when it refuses to
 * jump to stage-1. Named here so a boot log can be read without the source. */
#define EBLDR_FAIL_STAGE1_HASH  0xBAD1  /* stage-1 hash does not match the build-time value */
#define EBLDR_FAIL_STAGE1_READ  0xBAD2  /* the flash read of stage-1 failed */
#define EBLDR_FAIL_STAGE1_NO_IMAGE 0xBAD3 /* the build embedded no stage-1 image to verify against */
#define EBLDR_FAIL_BTLDR_REPLACE 0xBAD4 /* a stage-1 replacement was refused or failed (ADR-022) */

/* `detail` value of an EOS_LOG_RECOVERY_ENTER entry written when recovery was
 * entered on a port with no usable UART: eos_hal_uart_init() failed, or the
 * first eos_hal_uart_recv() failed with something other than a timeout. Such
 * a session cannot be reached and does not feed the watchdog. An entry with
 * detail 0 does not say the opposite: callers log RECOVERY_ENTER themselves,
 * and a session can write one with 0 before its first receive fails. */
#define EOS_RECOVERY_NO_TRANSPORT 0x4E4F5458  /* "NOTX" */

/* ---------------- RTOS Boot Types (Phase 2) ---------------- */

typedef enum {
    EOS_BOOT_MODE_LINUX  = 0,
    EOS_BOOT_MODE_RTOS   = 1,
    EOS_BOOT_MODE_HYBRID = 2,
} eos_boot_mode_t;

#define EOS_IMG_FLAG_RTOS         (1U << 3)
#define EOS_IMG_FLAG_LINUX        (1U << 4)
#define EOS_IMG_FLAG_SIGNED       (1U << 5)
#define EOS_IMG_FLAG_HASH_SHA256  (1U << 6)

#ifdef __cplusplus
}
#endif
#endif /* EOS_TYPES_H */
