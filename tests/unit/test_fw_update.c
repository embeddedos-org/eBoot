// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_fw_update.c
 * @brief Host tests for the firmware-update pipeline.
 *
 * The anti-rollback regression drives eos_fw_update_finalize() so the
 * production wiring in core/fw_update.c is what reads the authenticated
 * TLV counter and compares it to the hardware floor. The TLV tail is
 * delivered through eos_fw_update_write(), not poked into flash.
 */

#include "eos_fw_update.h"
#include "eos_image.h"
#include "eos_image_tlv.h"
#include "eos_crypto_boot.h"
#include "eos_bootctl.h"
#include "eos_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SIM_FLASH_SIZE   (128 * 1024)
#define SIM_SLOT_A_ADDR  0x00000u
#define SIM_SLOT_A_SIZE  0x08000u
#define SIM_SLOT_B_ADDR  0x10000u
#define SIM_SLOT_B_SIZE  0x08000u
#define PAYLOAD_SIZE     256u

static uint8_t sim_flash[SIM_FLASH_SIZE];
static uint32_t sim_counter;

static int sim_range_ok(uint32_t addr, size_t len)
{
    return len <= SIM_FLASH_SIZE && (size_t)addr <= SIM_FLASH_SIZE - len;
}

static int sim_flash_read(uint32_t addr, void *buf, size_t len)
{
    if (!sim_range_ok(addr, len)) return EOS_ERR_FLASH;
    memcpy(buf, &sim_flash[addr], len);
    return EOS_OK;
}

static int sim_flash_write(uint32_t addr, const void *buf, size_t len)
{
    if (!sim_range_ok(addr, len)) return EOS_ERR_FLASH;
    memcpy(&sim_flash[addr], buf, len);
    return EOS_OK;
}

static int sim_flash_erase(uint32_t addr, size_t len)
{
    if (!sim_range_ok(addr, len)) return EOS_ERR_FLASH;
    memset(&sim_flash[addr], 0xFF, len);
    return EOS_OK;
}

static int sim_monotonic_read(uint32_t *value)
{
    if (!value) return EOS_ERR_INVALID;
    *value = sim_counter;
    return EOS_OK;
}

static const eos_board_ops_t sim_ops = {
    .flash_base          = 0,
    .flash_size          = SIM_FLASH_SIZE,
    .slot_a_addr         = SIM_SLOT_A_ADDR,
    .slot_a_size         = SIM_SLOT_A_SIZE,
    .slot_b_addr         = SIM_SLOT_B_ADDR,
    .slot_b_size         = SIM_SLOT_B_SIZE,
    .bootctl_addr        = 0x0E000u,
    .bootctl_backup_addr = 0x0E200u,
    .log_addr            = 0x0E400u,
    .flash_read          = sim_flash_read,
    .flash_write         = sim_flash_write,
    .flash_erase         = sim_flash_erase,
    .monotonic_read      = sim_monotonic_read,
};

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
        eos_hal_init(&sim_ops); \
        printf("  %-58s ", #name); \
        name(); \
        tests_passed++; \
        printf("[PASS]\n"); \
    } \
    static void name(void)

/* [tlv_info(4)][entry_hdr(4)][uint32 value] */
#define TLV_AREA_LEN   (uint16_t)(sizeof(eos_tlv_info_t) + \
                                  sizeof(eos_tlv_entry_hdr_t) + sizeof(uint32_t))
#define TLV_VALUE_OFF  (uint32_t)(sizeof(eos_tlv_info_t) + sizeof(eos_tlv_entry_hdr_t))
#define STREAMED_LEN   (sizeof(eos_image_header_t) + PAYLOAD_SIZE)
#define IMAGE_BUF_LEN  (STREAMED_LEN + TLV_AREA_LEN)

static void fill_payload(uint8_t *payload)
{
    uint32_t i;
    for (i = 0; i < PAYLOAD_SIZE; i++)
        payload[i] = (uint8_t)(i * 7u + 1u);
}

static void build_image(uint8_t *out, uint32_t sec_ver)
{
    uint8_t payload[PAYLOAD_SIZE];

    fill_payload(payload);

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
    hdr.sig_type      = EOS_SIG_NONE;
    hdr.sig_len       = 0;

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

    memset(out, 0, IMAGE_BUF_LEN);
    memcpy(out, &hdr, sizeof(hdr));
    memcpy(out + sizeof(hdr), payload, PAYLOAD_SIZE);
    memcpy(out + STREAMED_LEN, tlv, TLV_AREA_LEN);
}

static void persist_bootctl(void)
{
    eos_bootctl_t bctl;
    eos_bootctl_init_defaults(&bctl);
    ASSERT(eos_bootctl_save(&bctl) == EOS_OK);
}

TEST(test_write_streams_tlv_then_finalize_rejects_below_floor)
{
    uint8_t image[IMAGE_BUF_LEN];
    eos_image_header_t hdr;
    eos_fw_update_ctx_t ctx;

    build_image(image, 3);
    memcpy(&hdr, image, sizeof(hdr));
    ASSERT(hdr.image_version == 0x00010000u);
    ASSERT(hdr.tlv_len == TLV_AREA_LEN);

    ASSERT(eos_fw_update_begin(&ctx, EOS_SLOT_B) == EOS_OK);
    ASSERT(eos_fw_update_write(&ctx, image, IMAGE_BUF_LEN) == EOS_OK);
    ASSERT(eos_fw_update_get_state(&ctx) == EOS_FW_STATE_VERIFY);
    ASSERT(ctx.tlv_written == TLV_AREA_LEN);

    sim_counter = 9;

    ASSERT(eos_fw_update_finalize(&ctx, EOS_UPGRADE_TEST) == EOS_ERR_ANTI_ROLLBACK);
}

TEST(test_write_does_not_reach_verify_until_tlv_arrives)
{
    uint8_t image[IMAGE_BUF_LEN];
    eos_fw_update_ctx_t ctx;

    build_image(image, 3);

    ASSERT(eos_fw_update_begin(&ctx, EOS_SLOT_B) == EOS_OK);
    ASSERT(eos_fw_update_write(&ctx, image, STREAMED_LEN) == EOS_OK);
    ASSERT(eos_fw_update_get_state(&ctx) == EOS_FW_STATE_TLV);
    ASSERT(eos_fw_update_finalize(&ctx, EOS_UPGRADE_TEST) == EOS_ERR_INVALID);
}

TEST(test_write_rejects_bytes_past_the_image_container)
{
    uint8_t image[IMAGE_BUF_LEN + 1];
    eos_fw_update_ctx_t ctx;

    build_image(image, 3);
    image[IMAGE_BUF_LEN] = 0xA5;

    ASSERT(eos_fw_update_begin(&ctx, EOS_SLOT_B) == EOS_OK);
    ASSERT(eos_fw_update_write(&ctx, image, IMAGE_BUF_LEN + 1) == EOS_ERR_INVALID);
}

/*
 * A block-framed transport asks how much of the container is still
 * outstanding so it can stop before its own padding. The header remainder
 * is all that is knowable until the header parses, because image_size and
 * tlv_len are fields inside it.
 */
TEST(test_bytes_wanted_tracks_the_remaining_container)
{
    uint8_t image[IMAGE_BUF_LEN];
    eos_fw_update_ctx_t ctx;

    build_image(image, 3);

    ASSERT(eos_fw_update_begin(&ctx, EOS_SLOT_B) == EOS_OK);
    ASSERT(eos_fw_update_bytes_wanted(&ctx) == sizeof(eos_image_header_t));

    ASSERT(eos_fw_update_write(&ctx, image, sizeof(eos_image_header_t)) == EOS_OK);
    ASSERT(eos_fw_update_get_state(&ctx) == EOS_FW_STATE_PAYLOAD);
    ASSERT(eos_fw_update_bytes_wanted(&ctx) == PAYLOAD_SIZE + TLV_AREA_LEN);

    ASSERT(eos_fw_update_write(&ctx, image + sizeof(eos_image_header_t),
                               PAYLOAD_SIZE) == EOS_OK);
    ASSERT(eos_fw_update_get_state(&ctx) == EOS_FW_STATE_TLV);
    ASSERT(eos_fw_update_bytes_wanted(&ctx) == TLV_AREA_LEN);

    ASSERT(eos_fw_update_write(&ctx, image + STREAMED_LEN, TLV_AREA_LEN) == EOS_OK);
    ASSERT(eos_fw_update_get_state(&ctx) == EOS_FW_STATE_VERIFY);
    ASSERT(eos_fw_update_bytes_wanted(&ctx) == 0);
}

/*
 * The trailing byte used to be treated differently depending on where the
 * chunk boundary fell: sharing a call with the last container byte drove the
 * context to ERROR, while arriving in its own call did not. Both are the
 * same protocol mistake and both must report it without discarding a
 * container that was already received in full.
 */
TEST(test_trailing_byte_is_rejected_the_same_across_chunk_boundaries)
{
    uint8_t image[IMAGE_BUF_LEN + 1];
    eos_fw_update_ctx_t single, split;
    int rc_single, rc_split;

    build_image(image, 3);
    image[IMAGE_BUF_LEN] = 0xA5;

    ASSERT(eos_fw_update_begin(&single, EOS_SLOT_B) == EOS_OK);
    rc_single = eos_fw_update_write(&single, image, IMAGE_BUF_LEN + 1);

    ASSERT(eos_fw_update_begin(&split, EOS_SLOT_B) == EOS_OK);
    ASSERT(eos_fw_update_write(&split, image, IMAGE_BUF_LEN) == EOS_OK);
    rc_split = eos_fw_update_write(&split, &image[IMAGE_BUF_LEN], 1);

    ASSERT(rc_single == EOS_ERR_INVALID);
    ASSERT(rc_split == rc_single);
    ASSERT(eos_fw_update_get_state(&single) == EOS_FW_STATE_VERIFY);
    ASSERT(eos_fw_update_get_state(&split) == eos_fw_update_get_state(&single));
    ASSERT(single.tlv_written == split.tlv_written);
}

TEST(test_finalize_accepts_tlv_counter_equal_to_floor)
{
    uint8_t image[IMAGE_BUF_LEN];
    eos_fw_update_ctx_t ctx;

    persist_bootctl();
    build_image(image, 9);

    ASSERT(eos_fw_update_begin(&ctx, EOS_SLOT_B) == EOS_OK);
    ASSERT(eos_fw_update_write(&ctx, image, IMAGE_BUF_LEN) == EOS_OK);
    ASSERT(eos_fw_update_get_state(&ctx) == EOS_FW_STATE_VERIFY);

    sim_counter = 9;

    ASSERT(eos_fw_update_finalize(&ctx, EOS_UPGRADE_TEST) == EOS_OK);
}

int main(void)
{
    printf("Firmware update (finalize anti-rollback)\n\n");

    run_test_write_streams_tlv_then_finalize_rejects_below_floor();
    run_test_write_does_not_reach_verify_until_tlv_arrives();
    run_test_write_rejects_bytes_past_the_image_container();
    run_test_bytes_wanted_tracks_the_remaining_container();
    run_test_trailing_byte_is_rejected_the_same_across_chunk_boundaries();
    run_test_finalize_accepts_tlv_counter_equal_to_floor();

    tests_run = 6;
    printf("\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
