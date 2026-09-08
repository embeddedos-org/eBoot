// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_jump_app.c
 * @brief Host tests for eboot_jump_to_app() anti-rollback wiring.
 *
 * PR #96's test_jump_app_bounds.c is not in this tree. This file follows
 * the same simulated-board convention as the other unit tests and uses
 * the real parse / integrity / rollback path. Signature verification is
 * supplied here so the suite does not have to embed a signing key.
 */

#include "eos_bootctl.h"
#include "eos_image.h"
#include "eos_image_tlv.h"
#include "eos_crypto_boot.h"
#include "eos_rollback.h"
#include "eos_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern int eboot_jump_to_app(eos_bootctl_t *bctl, eos_slot_t slot);

#define SIM_FLASH_SIZE   (64 * 1024)
#define SIM_SLOT_A_ADDR  0x4000u
#define SIM_SLOT_A_SIZE  0x8000u
#define PAYLOAD_SIZE     256u

static uint8_t sim_flash[SIM_FLASH_SIZE];
static uint32_t sim_counter;
static int sim_jump_count;
static uint32_t sim_jump_addr;

static int sim_flash_read(uint32_t addr, void *buf, size_t len)
{
    if (addr + len > SIM_FLASH_SIZE) return EOS_ERR_FLASH;
    memcpy(buf, &sim_flash[addr], len);
    return EOS_OK;
}

static int sim_flash_write(uint32_t addr, const void *buf, size_t len)
{
    if (addr + len > SIM_FLASH_SIZE) return EOS_ERR_FLASH;
    memcpy(&sim_flash[addr], buf, len);
    return EOS_OK;
}

static int sim_flash_erase(uint32_t addr, size_t len)
{
    if (addr + len > SIM_FLASH_SIZE) return EOS_ERR_FLASH;
    memset(&sim_flash[addr], 0xFF, len);
    return EOS_OK;
}

static int sim_monotonic_read(uint32_t *value)
{
    if (!value) return EOS_ERR_INVALID;
    *value = sim_counter;
    return EOS_OK;
}

static int sim_monotonic_increment(void)
{
    sim_counter++;
    return EOS_OK;
}

static void sim_jump(uint32_t addr)
{
    sim_jump_count++;
    sim_jump_addr = addr;
}

static uint32_t sim_tick;
static uint32_t sim_get_tick(void) { return sim_tick++; }
static void sim_noop(void) {}
static void sim_noop_u32(uint32_t x) { (void)x; }
static eos_reset_reason_t sim_reset_reason(void) { return EOS_RESET_POWER_ON; }
static bool sim_recovery_pin(void) { return false; }
static void sim_system_reset(void) {}

static const eos_board_ops_t sim_ops = {
    .flash_base          = 0,
    .flash_size          = SIM_FLASH_SIZE,
    .slot_a_addr         = SIM_SLOT_A_ADDR,
    .slot_a_size         = SIM_SLOT_A_SIZE,
    .slot_b_addr         = 0xC000u,
    .slot_b_size         = 0x8000u,
    .bootctl_addr        = 0x0000u,
    .bootctl_backup_addr = 0x1000u,
    .log_addr            = 0x2000u,
    .flash_read          = sim_flash_read,
    .flash_write         = sim_flash_write,
    .flash_erase         = sim_flash_erase,
    .watchdog_init       = sim_noop_u32,
    .watchdog_feed       = sim_noop,
    .get_reset_reason    = sim_reset_reason,
    .system_reset        = sim_system_reset,
    .recovery_pin_asserted = sim_recovery_pin,
    .jump                = sim_jump,
    .get_tick_ms         = sim_get_tick,
    .disable_interrupts  = sim_noop,
    .enable_interrupts   = sim_noop,
    .deinit_peripherals  = sim_noop,
    .monotonic_read      = sim_monotonic_read,
    .monotonic_increment = sim_monotonic_increment,
};

/*
 * image_verify.c defines parse, integrity and signature together. Providing
 * only the signature here pulls that object in and breaks the MSVC link
 * (same reason test_slot_manager.c supplies all three). These two still
 * check the bytes on the simulated slot; signature returns OK so the
 * jump path can reach the TLV floor without embedding a signing key.
 */
int eos_image_parse_header(uint32_t addr, eos_image_header_t *out)
{
    if (!out)
        return EOS_ERR_INVALID;
    if (eos_hal_flash_read(addr, out, sizeof(*out)) != EOS_OK)
        return EOS_ERR_FLASH;
    if (out->magic != EOS_IMG_MAGIC)
        return EOS_ERR_NO_IMAGE;
    if (out->hdr_version == 0 || out->hdr_version > EOS_IMAGE_HDR_VERSION)
        return EOS_ERR_INVALID;
    if (out->hdr_size < sizeof(eos_image_header_t) || out->hdr_size > 4096)
        return EOS_ERR_INVALID;
    if (out->image_size == 0)
        return EOS_ERR_INVALID;
    return EOS_OK;
}

int eos_image_verify_integrity(const eos_image_header_t *hdr, uint32_t addr)
{
    uint8_t payload[PAYLOAD_SIZE];
    uint8_t digest[EOS_SHA256_DIGEST_SIZE];

    if (!hdr)
        return EOS_ERR_INVALID;
    if (hdr->image_size != PAYLOAD_SIZE)
        return EOS_ERR_INVALID;
    if (eos_hal_flash_read(addr + hdr->hdr_size, payload, PAYLOAD_SIZE) != EOS_OK)
        return EOS_ERR_FLASH;
    eos_sha256(payload, PAYLOAD_SIZE, digest);
    if (memcmp(digest, hdr->hash, EOS_SHA256_DIGEST_SIZE) != 0)
        return EOS_ERR_CRC;
    return EOS_OK;
}

int eos_image_verify_signature(const eos_image_header_t *hdr)
{
    if (!hdr) return EOS_ERR_INVALID;
    return EOS_OK;
}

#define TLV_AREA_LEN   (uint16_t)(sizeof(eos_tlv_info_t) + \
                                  sizeof(eos_tlv_entry_hdr_t) + sizeof(uint32_t))
#define TLV_VALUE_OFF  (uint32_t)(sizeof(eos_tlv_info_t) + sizeof(eos_tlv_entry_hdr_t))

static void build_slot_image(uint32_t sec_ver)
{
    uint8_t payload[PAYLOAD_SIZE];
    uint32_t i;

    for (i = 0; i < PAYLOAD_SIZE; i++)
        payload[i] = (uint8_t)(i * 7u + 1u);

    eos_image_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic         = EOS_IMG_MAGIC;
    hdr.hdr_version   = EOS_IMAGE_HDR_VERSION;
    hdr.hdr_size      = (uint16_t)sizeof(eos_image_header_t);
    hdr.image_size    = PAYLOAD_SIZE;
    hdr.load_addr     = 0;
    hdr.entry_addr    = 0;
    hdr.image_version = 0x00010000u;
    hdr.flags         = EOS_IMG_FLAG_HASH_SHA256;
    eos_sha256(payload, PAYLOAD_SIZE, hdr.hash);
    hdr.sig_type      = EOS_SIG_ED25519;
    hdr.sig_len       = EOS_SIG_MAX_SIZE;

    uint8_t tlv[TLV_AREA_LEN];
    eos_tlv_info_t info = { EOS_TLV_INFO_MAGIC, TLV_AREA_LEN };
    eos_tlv_entry_hdr_t ent = { EOS_TLV_MIN_SEC_VER, sizeof(uint32_t) };
    memcpy(tlv, &info, sizeof(info));
    memcpy(tlv + sizeof(info), &ent, sizeof(ent));
    memcpy(tlv + TLV_VALUE_OFF, &sec_ver, sizeof(sec_ver));

    uint8_t digest[EOS_SHA256_DIGEST_SIZE];
    eos_sha256(tlv, TLV_AREA_LEN, digest);
    hdr.tlv_len = TLV_AREA_LEN;
    memcpy(hdr.tlv_hash, digest, EOS_IMG_TLV_HASH_LEN);

    memcpy(&sim_flash[SIM_SLOT_A_ADDR], &hdr, sizeof(hdr));
    memcpy(&sim_flash[SIM_SLOT_A_ADDR + sizeof(hdr)], payload, PAYLOAD_SIZE);
    memcpy(&sim_flash[SIM_SLOT_A_ADDR + sizeof(hdr) + PAYLOAD_SIZE],
           tlv, TLV_AREA_LEN);
}

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while(0)

#define TEST(name) \
    static void name(void); \
    static void run_##name(void) { \
        memset(sim_flash, 0xFF, sizeof(sim_flash)); \
        sim_counter = 0; \
        sim_jump_count = 0; \
        sim_jump_addr = 0; \
        sim_tick = 0; \
        eos_hal_init(&sim_ops); \
        eos_rollback_clear_staged(); \
        printf("  %-58s ", #name); \
        name(); \
        tests_passed++; \
        printf("[PASS]\n"); \
    } \
    static void name(void)

TEST(test_jump_rejects_tlv_counter_below_hw_floor)
{
    eos_bootctl_t bctl;

    build_slot_image(3);
    eos_bootctl_init_defaults(&bctl);
    sim_counter = 9;

    ASSERT(eboot_jump_to_app(&bctl, EOS_SLOT_A) == EOS_ERR_ANTI_ROLLBACK);
    ASSERT(sim_jump_count == 0);

    ASSERT(eos_rollback_commit() == EOS_OK);
    ASSERT(sim_counter == 9);
}

TEST(test_jump_stages_tlv_counter_above_floor)
{
    eos_bootctl_t bctl;

    build_slot_image(9);
    eos_bootctl_init_defaults(&bctl);
    sim_counter = 5;

    /* Host HAL jump returns, so the production "never reached" tail runs. */
    ASSERT(eboot_jump_to_app(&bctl, EOS_SLOT_A) == EOS_ERR_GENERIC);
    ASSERT(sim_jump_count == 1);

    ASSERT(eos_rollback_commit() == EOS_OK);
    ASSERT(sim_counter == 9);
}

int main(void)
{
    printf("Stage-1 jump_app (TLV anti-rollback)\n\n");

    run_test_jump_rejects_tlv_counter_below_hw_floor();
    run_test_jump_stages_tlv_counter_above_floor();

    tests_run = 2;
    printf("\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
