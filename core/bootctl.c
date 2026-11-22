// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file bootctl.c
 * @brief Boot control block load/save/validate operations
 */

#include "eos_bootctl.h"
#include "eos_hal.h"
#include "eos_image.h"
#include "eos_rollback.h"
#include <string.h>

/* Compute CRC32 over a byte buffer (software implementation) */
static uint32_t crc32_buf(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

static void bootctl_update_crc(eos_bootctl_t *bctl)
{
    bctl->crc32 = crc32_buf(bctl, offsetof(eos_bootctl_t, crc32));
}

/* ---- Boot control v1 layout (EOS_BOOTCTL_VERSION 1) ----
 *
 * v2 inserts eos_bootloader_replace_intent_t (68 bytes) after boot_count,
 * so a v1 copy fails the v2 CRC: its crc32 field sits where v2 keeps
 * intent/reserved bytes. Reading it as v2 and dropping the state would
 * reset the device's boot policy (active slot, confirmed slot, log head)
 * to defaults on the first boot after the update -- a device running its
 * B image would reboot into A. Migrate instead: read the v1-sized block,
 * validate its own CRC, and carry the fields forward. All fields are
 * uint32_t, so there is no padding to account for. */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t active_slot;
    uint32_t pending_slot;
    uint32_t confirmed_slot;
    uint32_t boot_attempts;
    uint32_t max_attempts;
    uint32_t last_reset_reason;
    uint32_t flags;
    uint32_t img_a_version;
    uint32_t img_b_version;
    uint32_t img_a_crc;
    uint32_t img_b_crc;
    uint32_t log_head;
    uint32_t boot_count;
    uint32_t reserved[7];
    uint32_t crc32;
} eos_bootctl_v1_t;

static bool bootctl_v1_validate(const eos_bootctl_v1_t *old)
{
    if (!old)
        return false;
    if (old->magic != EOS_BOOTCTL_MAGIC)
        return false;
    if (old->version != 1)
        return false;
    uint32_t computed = crc32_buf(old, offsetof(eos_bootctl_v1_t, crc32));
    return computed == old->crc32;
}

static void bootctl_migrate_v1(eos_bootctl_t *bctl,
                               const eos_bootctl_v1_t *old)
{
    memset(bctl, 0, sizeof(*bctl));
    bctl->magic            = EOS_BOOTCTL_MAGIC;
    bctl->version          = EOS_BOOTCTL_VERSION;
    bctl->active_slot      = old->active_slot;
    bctl->pending_slot     = old->pending_slot;
    bctl->confirmed_slot   = old->confirmed_slot;
    bctl->boot_attempts    = old->boot_attempts;
    bctl->max_attempts     = old->max_attempts;
    bctl->last_reset_reason = old->last_reset_reason;
    bctl->flags            = old->flags;
    bctl->img_a_version    = old->img_a_version;
    bctl->img_b_version    = old->img_b_version;
    bctl->img_a_crc        = old->img_a_crc;
    bctl->img_b_crc        = old->img_b_crc;
    bctl->log_head         = old->log_head;
    bctl->boot_count       = old->boot_count;
    /* replace_intent stays zeroed: no replacement was ever requested
     * under v1, and a zeroed record is no replacement. */
    bootctl_update_crc(bctl);
}

static int bootctl_load_v1(eos_bootctl_t *bctl, uint32_t addr)
{
    eos_bootctl_v1_t old;
    int rc = eos_hal_flash_read(addr, &old, sizeof(old));
    if (rc != EOS_OK)
        return rc;
    if (!bootctl_v1_validate(&old))
        return EOS_ERR_CRC;
    bootctl_migrate_v1(bctl, &old);
    return EOS_OK;
}

bool eos_bootctl_validate(const eos_bootctl_t *bctl)
{
    if (!bctl)
        return false;

    if (bctl->magic != EOS_BOOTCTL_MAGIC)
        return false;

    uint32_t computed = crc32_buf(bctl, offsetof(eos_bootctl_t, crc32));
    return computed == bctl->crc32;
}

void eos_bootctl_init_defaults(eos_bootctl_t *bctl)
{
    memset(bctl, 0, sizeof(*bctl));
    bctl->magic          = EOS_BOOTCTL_MAGIC;
    bctl->version        = EOS_BOOTCTL_VERSION;
    bctl->active_slot    = EOS_SLOT_A;
    bctl->pending_slot   = EOS_SLOT_NONE;
    bctl->confirmed_slot = EOS_SLOT_NONE;
    bctl->boot_attempts  = 0;
    bctl->max_attempts   = EOS_MAX_BOOT_ATTEMPTS;
    bctl->flags          = 0;
    bctl->log_head       = 0;
    bctl->boot_count     = 0;
    bootctl_update_crc(bctl);
}

int eos_bootctl_load(eos_bootctl_t *bctl)
{
    const eos_board_ops_t *ops = eos_hal_get_ops();
    if (!ops)
        return EOS_ERR_GENERIC;

    /* Try primary copy */
    int rc = eos_hal_flash_read(ops->bootctl_addr, bctl, sizeof(*bctl));
    if (rc == EOS_OK && eos_bootctl_validate(bctl))
        return EOS_OK;

    /* Try backup copy */
    rc = eos_hal_flash_read(ops->bootctl_backup_addr, bctl, sizeof(*bctl));
    if (rc == EOS_OK && eos_bootctl_validate(bctl))
        return EOS_OK;

    /* Both v2 copies are unreadable or corrupt. A v1 copy fails the v2
     * CRC by layout, not by damage: migrate it instead of dropping the
     * device's boot state to defaults (see bootctl_v1_validate). */
    rc = bootctl_load_v1(bctl, ops->bootctl_addr);
    if (rc == EOS_OK)
        return EOS_OK;
    rc = bootctl_load_v1(bctl, ops->bootctl_backup_addr);
    if (rc == EOS_OK)
        return EOS_OK;

    /* Both copies corrupt — initialize defaults */
    eos_bootctl_init_defaults(bctl);
    return EOS_ERR_CRC;
}

int eos_bootctl_save(eos_bootctl_t *bctl)
{
    const eos_board_ops_t *ops = eos_hal_get_ops();
    if (!ops)
        return EOS_ERR_GENERIC;

    bootctl_update_crc(bctl);

    /* Erase and write primary */
    int rc = eos_hal_flash_erase(ops->bootctl_addr, sizeof(*bctl));
    if (rc != EOS_OK)
        return EOS_ERR_FLASH;

    rc = eos_hal_flash_write(ops->bootctl_addr, bctl, sizeof(*bctl));
    if (rc != EOS_OK)
        return EOS_ERR_FLASH;

    /* Erase and write backup */
    rc = eos_hal_flash_erase(ops->bootctl_backup_addr, sizeof(*bctl));
    if (rc != EOS_OK)
        return EOS_ERR_FLASH;

    rc = eos_hal_flash_write(ops->bootctl_backup_addr, bctl, sizeof(*bctl));
    if (rc != EOS_OK)
        return EOS_ERR_FLASH;

    return EOS_OK;
}

int eos_bootctl_increment_attempts(eos_bootctl_t *bctl)
{
    bctl->boot_attempts++;
    bctl->boot_count++;
    return eos_bootctl_save(bctl);
}

int eos_bootctl_reset_attempts(eos_bootctl_t *bctl)
{
    bctl->boot_attempts = 0;
    return eos_bootctl_save(bctl);
}

int eos_bootctl_set_pending(eos_bootctl_t *bctl, eos_slot_t slot)
{
    bctl->pending_slot = slot;
    bctl->flags |= EOS_FLAG_UPGRADE_PENDING;
    return eos_bootctl_save(bctl);
}

int eos_bootctl_clear_pending(eos_bootctl_t *bctl)
{
    bctl->pending_slot = EOS_SLOT_NONE;
    bctl->flags &= ~EOS_FLAG_UPGRADE_PENDING;
    return eos_bootctl_save(bctl);
}

int eos_bootctl_confirm(eos_bootctl_t *bctl)
{
    bctl->confirmed_slot = bctl->active_slot;
    bctl->flags |= EOS_FLAG_CONFIRMED;
    bctl->flags &= ~EOS_FLAG_TEST_BOOT;
    bctl->boot_attempts = 0;

    int rc = eos_bootctl_save(bctl);
    if (rc != EOS_OK) return rc;

    /* The image has proven itself, so the previous one is no longer needed as
     * a fallback and the anti-rollback floor can safely be raised.
     *
     * A failure is not propagated: the slot is confirmed and that must stick.
     * The condition is observable via eos_rollback_get_device_counter(). */
    (void)eos_rollback_commit();

    return EOS_OK;
}

int eos_bootctl_request_recovery(eos_bootctl_t *bctl)
{
    bctl->flags |= EOS_FLAG_FORCE_RECOVERY;
    return eos_bootctl_save(bctl);
}

int eos_bootctl_request_factory_reset(eos_bootctl_t *bctl)
{
    bctl->flags |= EOS_FLAG_FACTORY_RESET;
    return eos_bootctl_save(bctl);
}

eos_slot_t eos_bootctl_other_slot(eos_slot_t slot)
{
    switch (slot) {
    case EOS_SLOT_A: return EOS_SLOT_B;
    case EOS_SLOT_B: return EOS_SLOT_A;
    default:         return EOS_SLOT_NONE;
    }
}
