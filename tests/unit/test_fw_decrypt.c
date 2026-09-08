// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_fw_decrypt.c
 * @brief Unit tests for streaming AES-256-GCM firmware decryption
 *
 * core/fw_decrypt.c is a hand-written AES-256-GCM in the secure boot path and
 * had no tests at all. These pin it against reference vectors produced by an
 * independent implementation (Python `cryptography`, i.e. OpenSSL), for the
 * cases a streaming decryptor actually meets: whole blocks, a partial tail,
 * and callers that split the stream on boundaries that are not multiples of
 * 16 -- GCM is a stream, so the result must not depend on how it was sliced.
 */

#include "eos_fw_decrypt.h"
#include "eos_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void name(void); \
    static void run_##name(void) { \
        printf("  %-52s ", #name); \
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

/* ---- Reference vectors (AES-256-GCM, 96-bit IV, no AAD) ---- */
static const uint8_t vec_key[32] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f};
static const uint8_t vec_iv[12] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b};
/* v16: 16 bytes */
static const uint8_t v16_ct[] = {0x47,0x03,0xd4,0x18,0xc1,0xe0,0xc4,0x1c,0x85,0x48,0x9d,0x80,0xbd,0xe4,0x76,0x62};
static const uint8_t v16_pt[] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
static const uint8_t v16_tag[] = {0xed,0x39,0x55,0x08,0x27,0x6f,0xf6,0x60,0x85,0x0d,0x12,0xd3,0xe7,0x55,0xeb,0xa5};
/* v32: 32 bytes */
static const uint8_t v32_ct[] = {0x47,0x03,0xd4,0x18,0xc1,0xe0,0xc4,0x1c,0x85,0x48,0x9d,0x80,0xbd,0xe4,0x76,0x62,0x93,0xc7,0x95,0x27,0xe4,0x6e,0x49,0x6b,0x20,0x7e,0xff,0x9e,0x01,0x74,0x1e,0xad};
static const uint8_t v32_pt[] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f};
static const uint8_t v32_tag[] = {0x5e,0xdd,0xdc,0x50,0x74,0x04,0x4e,0x22,0x82,0xb4,0x32,0xb3,0xf2,0xd8,0xf6,0x73};
/* v20: 20 bytes */
static const uint8_t v20_ct[] = {0x47,0x03,0xd4,0x18,0xc1,0xe0,0xc4,0x1c,0x85,0x48,0x9d,0x80,0xbd,0xe4,0x76,0x62,0x93,0xc7,0x95,0x27};
static const uint8_t v20_pt[] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13};
static const uint8_t v20_tag[] = {0x47,0x60,0x36,0x9f,0x8e,0x14,0x73,0x21,0x58,0x5e,0x06,0x47,0x37,0x46,0x3f,0x95};

/* ---- Simulated board ----
 * Only OTP is provided: eos_fw_decrypt_init() reads the key from it. */

#define OTP_FW_KEY_OFFSET 0x200

static int otp_rc = EOS_OK;
static uint8_t otp_key[EOS_AES_KEY_SIZE];

static int sim_otp_read(uint32_t offset, void *buf, size_t len)
{
    if (otp_rc != EOS_OK) return otp_rc;
    if (offset != OTP_FW_KEY_OFFSET || len != EOS_AES_KEY_SIZE)
        return EOS_ERR_INVALID;
    memcpy(buf, otp_key, EOS_AES_KEY_SIZE);
    return EOS_OK;
}

/* A board whose AES engine works: it returns the correct plaintext and reports
 * success. That matters -- an engine that returns an error is not exercising
 * anything, because the caller just falls through to the software path. The
 * bug this pins only appears when the engine SUCCEEDS: the old code then
 * returned early, having skipped GHASH entirely, and eos_fw_decrypt_final()
 * refused a genuine image.
 *
 * Only the reference vectors are ever passed here, so returning their known
 * plaintext is a faithful stand-in for a working engine. */
static int sim_hw_aes_decrypt(const void *key, size_t key_len, const void *iv,
                              const void *in, void *out, size_t len)
{
    (void)key; (void)key_len; (void)iv; (void)in;

    if (len == sizeof v32_pt)      memcpy(out, v32_pt, len);
    else if (len == sizeof v16_pt) memcpy(out, v16_pt, len);
    else if (len == sizeof v20_pt) memcpy(out, v20_pt, len);
    else return EOS_ERR_NOT_SUPPORTED;

    return EOS_OK;
}

static eos_board_ops_t sim_ops;

static void reset_fixture(int with_aes_engine)
{
    memset(&sim_ops, 0, sizeof(sim_ops));
    sim_ops.otp_read = sim_otp_read;
    if (with_aes_engine) sim_ops.hw_aes_decrypt = sim_hw_aes_decrypt;
    memcpy(otp_key, vec_key, sizeof(otp_key));
    otp_rc = EOS_OK;
    eos_hal_init(&sim_ops);
}

/* Decrypt `ct` in `chunk`-sized calls and check plaintext and tag. */
static void decrypt_and_check(const uint8_t *ct, size_t ctlen,
                              const uint8_t *pt, const uint8_t *tag,
                              const size_t *splits, int nsplits)
{
    uint8_t buf[64];
    ASSERT(ctlen <= sizeof(buf));
    memcpy(buf, ct, ctlen);

    eos_fw_decrypt_ctx_t ctx;
    ASSERT(eos_fw_decrypt_init(&ctx, vec_iv) == EOS_OK);

    size_t off = 0;
    for (int i = 0; i < nsplits && off < ctlen; i++) {
        size_t n = splits[i];
        if (off + n > ctlen) n = ctlen - off;
        ASSERT(eos_fw_decrypt_update(&ctx, buf + off, n) == EOS_OK);
        off += n;
    }
    ASSERT(off == ctlen);

    ASSERT(memcmp(buf, pt, ctlen) == 0);
    ASSERT(eos_fw_decrypt_final(&ctx, tag) == EOS_OK);
}

TEST(test_single_block_matches_reference)
{
    reset_fixture(0);
    const size_t one[] = { 64 };
    decrypt_and_check(v16_ct, sizeof v16_ct, v16_pt, v16_tag, one, 1);
}

TEST(test_partial_trailing_block_matches_reference)
{
    reset_fixture(0);
    const size_t one[] = { 64 };
    decrypt_and_check(v20_ct, sizeof v20_ct, v20_pt, v20_tag, one, 1);
}

/* GCM is a stream. Feeding the same ciphertext in different chunk sizes must
 * give the same plaintext and the same tag, including splits that land inside
 * a block. */
TEST(test_result_is_independent_of_chunk_boundaries)
{
    reset_fixture(0);
    const size_t whole[]   = { 32 };
    const size_t aligned[] = { 16, 16 };
    const size_t inside[]  = { 10, 22 };
    const size_t byte1[]   = { 1, 31 };
    decrypt_and_check(v32_ct, sizeof v32_ct, v32_pt, v32_tag, whole, 1);
    decrypt_and_check(v32_ct, sizeof v32_ct, v32_pt, v32_tag, aligned, 2);
    decrypt_and_check(v32_ct, sizeof v32_ct, v32_pt, v32_tag, inside, 2);
    decrypt_and_check(v32_ct, sizeof v32_ct, v32_pt, v32_tag, byte1, 2);

    const size_t tail[] = { 5, 15 };
    decrypt_and_check(v20_ct, sizeof v20_ct, v20_pt, v20_tag, tail, 2);
}

/* A board advertising an AES engine must get the same answer as one without.
 * The HW shortcut skipped GHASH, so eos_fw_decrypt_final() computed the tag
 * over an empty accumulator and refused a correctly encrypted image. */
TEST(test_board_with_aes_engine_still_accepts_a_genuine_image)
{
    reset_fixture(1);
    const size_t one[] = { 64 };
    decrypt_and_check(v32_ct, sizeof v32_ct, v32_pt, v32_tag, one, 1);
    decrypt_and_check(v16_ct, sizeof v16_ct, v16_pt, v16_tag, one, 1);
    decrypt_and_check(v20_ct, sizeof v20_ct, v20_pt, v20_tag, one, 1);
}

TEST(test_tampered_tag_is_rejected)
{
    reset_fixture(0);
    uint8_t buf[32];
    eos_fw_decrypt_ctx_t ctx;

    /* Every single-bit flip in the tag must be caught. */
    for (int bit = 0; bit < 8 * (int)EOS_AES_TAG_SIZE; bit++) {
        uint8_t bad[EOS_AES_TAG_SIZE];
        memcpy(bad, v32_tag, sizeof(bad));
        bad[bit / 8] ^= (uint8_t)(1u << (bit % 8));

        memcpy(buf, v32_ct, sizeof(buf));
        ASSERT(eos_fw_decrypt_init(&ctx, vec_iv) == EOS_OK);
        ASSERT(eos_fw_decrypt_update(&ctx, buf, sizeof(buf)) == EOS_OK);
        ASSERT(eos_fw_decrypt_final(&ctx, bad) != EOS_OK);
    }

    uint8_t zeros[EOS_AES_TAG_SIZE] = {0};
    memcpy(buf, v32_ct, sizeof(buf));
    ASSERT(eos_fw_decrypt_init(&ctx, vec_iv) == EOS_OK);
    ASSERT(eos_fw_decrypt_update(&ctx, buf, sizeof(buf)) == EOS_OK);
    ASSERT(eos_fw_decrypt_final(&ctx, zeros) != EOS_OK);
}

/* Tampering with the ciphertext must change the tag. */
TEST(test_tampered_ciphertext_is_rejected)
{
    reset_fixture(0);
    for (size_t i = 0; i < sizeof v32_ct; i++) {
        uint8_t buf[32];
        memcpy(buf, v32_ct, sizeof(buf));
        buf[i] ^= 0x01;

        eos_fw_decrypt_ctx_t ctx;
        ASSERT(eos_fw_decrypt_init(&ctx, vec_iv) == EOS_OK);
        ASSERT(eos_fw_decrypt_update(&ctx, buf, sizeof(buf)) == EOS_OK);
        ASSERT(eos_fw_decrypt_final(&ctx, v32_tag) != EOS_OK);
    }
}

TEST(test_init_rejects_bad_arguments_and_unprovisioned_keys)
{
    reset_fixture(0);
    eos_fw_decrypt_ctx_t ctx;

    ASSERT(eos_fw_decrypt_init(NULL, vec_iv) != EOS_OK);
    ASSERT(eos_fw_decrypt_init(&ctx, NULL) != EOS_OK);

    /* An all-zero OTP word means the key was never provisioned. */
    memset(otp_key, 0, sizeof(otp_key));
    ASSERT(eos_fw_decrypt_init(&ctx, vec_iv) != EOS_OK);

    /* An OTP that cannot be read is not a reason to decrypt with garbage. */
    memcpy(otp_key, vec_key, sizeof(otp_key));
    otp_rc = EOS_ERR_FLASH;
    ASSERT(eos_fw_decrypt_init(&ctx, vec_iv) != EOS_OK);
    otp_rc = EOS_OK;
}

TEST(test_update_and_final_reject_uninitialised_contexts)
{
    reset_fixture(0);
    eos_fw_decrypt_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));   /* initialized == false */

    uint8_t buf[16] = {0};
    ASSERT(eos_fw_decrypt_update(&ctx, buf, sizeof(buf)) != EOS_OK);
    ASSERT(eos_fw_decrypt_final(&ctx, v16_tag) != EOS_OK);

    ASSERT(eos_fw_decrypt_init(&ctx, vec_iv) == EOS_OK);
    ASSERT(eos_fw_decrypt_update(&ctx, NULL, 16) != EOS_OK);
    ASSERT(eos_fw_decrypt_final(&ctx, NULL) != EOS_OK);
}

int main(void)
{
    printf("=== eBootloader: Firmware Decryption (AES-256-GCM) Tests ===\n\n");

    run_test_single_block_matches_reference();
    run_test_partial_trailing_block_matches_reference();
    run_test_result_is_independent_of_chunk_boundaries();
    run_test_board_with_aes_engine_still_accepts_a_genuine_image();
    run_test_tampered_tag_is_rejected();
    run_test_tampered_ciphertext_is_rejected();
    run_test_init_rejects_bad_arguments_and_unprovisioned_keys();
    run_test_update_and_final_reject_uninitialised_contexts();

    tests_run = 8;
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
