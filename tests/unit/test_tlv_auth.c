// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_tlv_auth.c
 * @brief Regression test: the TLV area feeding anti-rollback must be
 *        authenticated by the signed image header.
 *
 * eos_rollback_read_image_counter() takes the image's security counter from
 * the EOS_TLV_MIN_SEC_VER entry in the TLV area, which lives at
 *   image_addr + hdr_size + image_size
 * i.e. immediately after the payload.
 *
 * Nothing used to cover those bytes:
 *   - eos_image_verify_signature() signs header[0, EOS_IMG_SIGNED_LEN) = [0,92)
 *   - eos_image_verify_integrity() hashes [hdr_size, hdr_size + image_size)
 *
 * The TLV area is disjoint from both. An attacker able to write flash could
 * therefore take a genuinely signed *old* image, raise its declared counter,
 * and walk it past eos_rollback_verify() — defeating the anti-rollback gate
 * that exists precisely to stop that downgrade — without disturbing a single
 * byte the signature or the payload hash covers.
 *
 * The fix binds the TLV area to the signed header via hdr.tlv_len and
 * hdr.tlv_hash (both inside the signed prefix). This test pins that:
 *   1. an authenticated TLV area is still read normally;
 *   2. tampering with it is now detected and fails closed;
 *   3. an image that declares no authenticated TLV area reports counter 0
 *      rather than trusting whatever bytes happen to follow the payload;
 *   4. the tampering in (2) really is invisible to signature/integrity
 *      coverage, so the header binding is the only thing that catches it.
 */

#include "eos_image.h"
#include "eos_image_tlv.h"
#include "eos_rollback.h"
#include "eos_crypto_boot.h"
#include "eos_hal.h"
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#define SIM_FLASH_SIZE  (64 * 1024)
#define SLOT_A_ADDR     0x1000u
#define SLOT_A_SIZE     0x8000u
#define PAYLOAD_SIZE    256u

static uint8_t sim_flash[SIM_FLASH_SIZE];

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

static uint32_t sim_counter = 0;
static int sim_monotonic_read(uint32_t *value)
{
    if (!value) return EOS_ERR_INVALID;
    *value = sim_counter;
    return EOS_OK;
}

static const eos_board_ops_t sim_ops = {
    .flash_base     = 0,
    .flash_size     = SIM_FLASH_SIZE,
    .slot_a_addr    = SLOT_A_ADDR,
    .slot_a_size    = SLOT_A_SIZE,
    .flash_read     = sim_flash_read,
    .flash_write    = sim_flash_write,
    .flash_erase    = sim_flash_erase,
    .monotonic_read = sim_monotonic_read,
};

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void name(void); \
    static void run_##name(void) { \
        memset(sim_flash, 0xFF, sizeof(sim_flash)); \
        sim_counter = 0; \
        eos_hal_init(&sim_ops); \
        printf("  %-58s ", #name); \
        tests_run++; \
        name(); \
        tests_passed++; \
        printf("[PASS]\n"); \
    } \
    static void name(void)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while(0)

/* ------------------------------------------------------------------
 * Image construction
 * ------------------------------------------------------------------ */

/* Byte offset of the TLV area, and of the MIN_SEC_VER value inside it. */
static uint32_t tlv_offset(void)
{
    return SLOT_A_ADDR + sizeof(eos_image_header_t) + PAYLOAD_SIZE;
}

/* [tlv_info(4)][entry_hdr(4)][uint32 value] */
#define TLV_AREA_LEN   (uint16_t)(sizeof(eos_tlv_info_t) + \
                                  sizeof(eos_tlv_entry_hdr_t) + sizeof(uint32_t))
#define TLV_VALUE_OFF  (uint32_t)(sizeof(eos_tlv_info_t) + sizeof(eos_tlv_entry_hdr_t))

/**
 * Lay a well-formed image into the simulated flash.
 *
 * @param sec_ver         value written into the MIN_SEC_VER TLV
 * @param emit_tlv        write a TLV area after the payload at all
 * @param bind_tlv        record tlv_len/tlv_hash in the header, i.e. sign it
 */
static void build_image(uint32_t sec_ver, bool emit_tlv, bool bind_tlv)
{
    uint8_t payload[PAYLOAD_SIZE];
    for (uint32_t i = 0; i < PAYLOAD_SIZE; i++)
        payload[i] = (uint8_t)(i * 7u + 1u);

    eos_image_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic         = EOS_IMG_MAGIC;
    hdr.hdr_version   = EOS_IMAGE_HDR_VERSION;
    hdr.hdr_size      = (uint16_t)sizeof(eos_image_header_t);
    hdr.image_size    = PAYLOAD_SIZE;
    hdr.load_addr     = 0;              /* skips the entry_addr range check */
    hdr.entry_addr    = 0;
    hdr.image_version = 0x00010000u;
    hdr.flags         = EOS_IMG_FLAG_HASH_SHA256;
    eos_sha256(payload, PAYLOAD_SIZE, hdr.hash);
    hdr.sig_type      = EOS_SIG_ED25519;
    hdr.sig_len       = EOS_SIG_MAX_SIZE;

    uint8_t tlv[TLV_AREA_LEN];
    if (emit_tlv) {
        eos_tlv_info_t info = { EOS_TLV_INFO_MAGIC, TLV_AREA_LEN };
        eos_tlv_entry_hdr_t ent = { EOS_TLV_MIN_SEC_VER, sizeof(uint32_t) };
        memcpy(tlv, &info, sizeof(info));
        memcpy(tlv + sizeof(info), &ent, sizeof(ent));
        memcpy(tlv + TLV_VALUE_OFF, &sec_ver, sizeof(sec_ver));

        if (bind_tlv) {
            uint8_t digest[EOS_SHA256_DIGEST_SIZE];
            eos_sha256(tlv, TLV_AREA_LEN, digest);
            hdr.tlv_len = TLV_AREA_LEN;
            memcpy(hdr.tlv_hash, digest, EOS_IMG_TLV_HASH_LEN);
        }
    }

    memcpy(&sim_flash[SLOT_A_ADDR], &hdr, sizeof(hdr));
    memcpy(&sim_flash[SLOT_A_ADDR + sizeof(hdr)], payload, PAYLOAD_SIZE);
    if (emit_tlv)
        memcpy(&sim_flash[tlv_offset()], tlv, TLV_AREA_LEN);
}

/* Rewrite the MIN_SEC_VER value in place, exactly as an attacker with flash
 * write access would: no header byte and no payload byte is touched. */
static void tamper_sec_ver(uint32_t new_value)
{
    memcpy(&sim_flash[tlv_offset() + TLV_VALUE_OFF], &new_value, sizeof(new_value));
}

/* ------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------ */

TEST(test_authenticated_tlv_counter_is_read)
{
    build_image(7, true, true);

    uint32_t counter = 0xDEADBEEF;
    ASSERT(eos_rollback_read_image_counter(SLOT_A_ADDR, &counter) == EOS_OK);
    ASSERT(counter == 7);
}

TEST(test_tampered_tlv_counter_is_rejected)
{
    build_image(3, true, true);

    /* Sanity: it reads as signed before the tamper. */
    uint32_t counter = 0;
    ASSERT(eos_rollback_read_image_counter(SLOT_A_ADDR, &counter) == EOS_OK);
    ASSERT(counter == 3);

    /* The downgrade attack: raise the declared counter past the device floor
     * so an old, genuinely signed image sails through eos_rollback_verify(). */
    sim_counter = 9;
    ASSERT(eos_rollback_verify(3) == EOS_ERR_ANTI_ROLLBACK);

    tamper_sec_ver(9);

    counter = 0;
    int rc = eos_rollback_read_image_counter(SLOT_A_ADDR, &counter);

    /* Fail closed. Before the header binding this returned EOS_OK with
     * counter == 9, and eos_rollback_verify(9) then accepted the image. */
    ASSERT(rc == EOS_ERR_INVALID);
}

TEST(test_tamper_is_invisible_to_signature_and_integrity)
{
    build_image(3, true, true);

    eos_image_header_t hdr;
    ASSERT(eos_image_parse_header(SLOT_A_ADDR, &hdr) == EOS_OK);
    ASSERT(eos_image_verify_integrity(&hdr, SLOT_A_ADDR) == EOS_OK);

    uint8_t hdr_before[sizeof(eos_image_header_t)];
    memcpy(hdr_before, &sim_flash[SLOT_A_ADDR], sizeof(hdr_before));

    tamper_sec_ver(9);

    /* The payload hash still matches and every byte of the signed prefix is
     * unchanged: neither of the bootloader's two cryptographic checks can see
     * this edit. That is why the counter needs its own binding. */
    ASSERT(eos_image_verify_integrity(&hdr, SLOT_A_ADDR) == EOS_OK);
    ASSERT(memcmp(hdr_before, &sim_flash[SLOT_A_ADDR], EOS_IMG_SIGNED_LEN) == 0);
}

TEST(test_unbound_tlv_area_is_not_trusted)
{
    /* A TLV area sitting after the payload that the header does not vouch for
     * is attacker-supplied as far as the bootloader can tell. It must not be
     * able to raise the counter; 0 is the conservative reading. */
    build_image(9, true, false);

    uint32_t counter = 0xDEADBEEF;
    ASSERT(eos_rollback_read_image_counter(SLOT_A_ADDR, &counter) == EOS_OK);
    ASSERT(counter == 0);

    sim_counter = 5;
    ASSERT(eos_rollback_verify(counter) == EOS_ERR_ANTI_ROLLBACK);
}

TEST(test_image_without_tlv_area_still_reports_zero)
{
    /* Unchanged behaviour for images from tools/sign_image.py, which emits
     * [header][payload] and no TLV area at all. */
    build_image(0, false, false);

    uint32_t counter = 0xDEADBEEF;
    ASSERT(eos_rollback_read_image_counter(SLOT_A_ADDR, &counter) == EOS_OK);
    ASSERT(counter == 0);
}

TEST(test_tlv_binding_fields_are_inside_the_signed_prefix)
{
    /* The binding is only worth anything if the signature covers it. */
    ASSERT(offsetof(eos_image_header_t, tlv_len) +
           sizeof(((eos_image_header_t *)0)->tlv_len) <= EOS_IMG_SIGNED_LEN);
    ASSERT(offsetof(eos_image_header_t, tlv_hash) +
           sizeof(((eos_image_header_t *)0)->tlv_hash) <= EOS_IMG_SIGNED_LEN);
}

TEST(test_oversized_tlv_len_is_rejected)
{
    build_image(7, true, true);

    /* A header claiming a TLV area larger than the parser will ever accept
     * must be refused outright, not hashed over an unbounded flash range. */
    eos_image_header_t hdr;
    memcpy(&hdr, &sim_flash[SLOT_A_ADDR], sizeof(hdr));
    hdr.tlv_len = (uint16_t)(EOS_TLV_MAX_SIZE + 1);
    memcpy(&sim_flash[SLOT_A_ADDR], &hdr, sizeof(hdr));

    uint32_t counter = 0;
    ASSERT(eos_rollback_read_image_counter(SLOT_A_ADDR, &counter) == EOS_ERR_INVALID);
}

int main(void)
{
    printf("TLV authentication (anti-rollback counter)\n\n");

    run_test_authenticated_tlv_counter_is_read();
    run_test_tampered_tlv_counter_is_rejected();
    run_test_tamper_is_invisible_to_signature_and_integrity();
    run_test_unbound_tlv_area_is_not_trusted();
    run_test_image_without_tlv_area_still_reports_zero();
    run_test_tlv_binding_fields_are_inside_the_signed_prefix();
    run_test_oversized_tlv_len_is_rejected();

    printf("\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
