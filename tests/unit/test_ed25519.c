// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_ed25519.c
 * @brief Unit tests for Ed25519 signature verification (RFC 8032)
 *
 * These tests assert BOTH directions. An earlier revision of this file
 * checked only that bad signatures were rejected, which a verifier that
 * rejects everything passes trivially -- and that is exactly the defect it
 * failed to catch. The RFC 8032 section 7.1 vectors below must be ACCEPTED,
 * so a broken verifier fails here.
 */

#include "eos_crypto_boot.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern int eos_ed25519_verify(const uint8_t signature[64],
                              const uint8_t public_key[32],
                              const uint8_t *message,
                              size_t msg_len);

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void name(void); \
    static void run_##name(void) { \
        printf("  %-50s ", #name); \
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

/* ---- helpers ---- */

static void hex2bin(const char *hex, uint8_t *out, size_t out_len)
{
    for (size_t i = 0; i < out_len; i++) {
        unsigned v = 0;
        char b[3] = { hex[2 * i], hex[2 * i + 1], 0 };
        v = (unsigned)strtoul(b, NULL, 16);
        out[i] = (uint8_t)v;
    }
}

/* RFC 8032 section 7.1 test vectors: public key, signature, message. */
struct rfc_vector {
    const char *name;
    const char *pubkey_hex;   /* 64 hex chars  */
    const char *sig_hex;      /* 128 hex chars */
    const char *msg_hex;      /* may be empty  */
};

static const struct rfc_vector k_vectors[] = {
    {
        "TEST 1 (empty message)",
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8"
        "821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b",
        ""
    },
    {
        "TEST 2 (1-byte message)",
        "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
        "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085a"
        "c1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00",
        "72"
    },
    {
        "TEST 3 (2-byte message)",
        "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
        "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff"
        "9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a",
        "af82"
    },
};

#define N_VECTORS (sizeof(k_vectors) / sizeof(k_vectors[0]))

/* ---- positive tests: a conforming signature MUST be accepted ---- */

TEST(test_ed25519_rfc8032_vectors_accepted)
{
    for (size_t i = 0; i < N_VECTORS; i++) {
        const struct rfc_vector *v = &k_vectors[i];
        uint8_t pk[32], sig[64], msg[8];
        size_t msg_len = strlen(v->msg_hex) / 2;

        hex2bin(v->pubkey_hex, pk, 32);
        hex2bin(v->sig_hex, sig, 64);
        if (msg_len) hex2bin(v->msg_hex, msg, msg_len);

        int rc = eos_ed25519_verify(sig, pk, msg, msg_len);
        if (rc != EOS_OK) {
            printf("\n    vector '%s' rejected (rc=%d)\n", v->name, rc);
        }
        ASSERT(rc == EOS_OK);
    }
}

/* ---- negative tests derived from the same vectors ---- */

TEST(test_ed25519_tampered_message_rejected)
{
    const struct rfc_vector *v = &k_vectors[1];   /* message 0x72 */
    uint8_t pk[32], sig[64], msg[1];

    hex2bin(v->pubkey_hex, pk, 32);
    hex2bin(v->sig_hex, sig, 64);
    hex2bin(v->msg_hex, msg, 1);

    ASSERT(eos_ed25519_verify(sig, pk, msg, 1) == EOS_OK);   /* baseline */

    msg[0] ^= 0x01;                                          /* one bit */
    ASSERT(eos_ed25519_verify(sig, pk, msg, 1) != EOS_OK);
}

TEST(test_ed25519_every_signature_bit_flip_rejected)
{
    const struct rfc_vector *v = &k_vectors[1];
    uint8_t pk[32], sig[64], msg[1];

    hex2bin(v->pubkey_hex, pk, 32);
    hex2bin(v->msg_hex, msg, 1);

    for (int byte = 0; byte < 64; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            hex2bin(v->sig_hex, sig, 64);
            sig[byte] ^= (uint8_t)(1u << bit);
            ASSERT(eos_ed25519_verify(sig, pk, msg, 1) != EOS_OK);
        }
    }
}

TEST(test_ed25519_wrong_public_key_rejected)
{
    uint8_t pk[32], sig[64], msg[1];

    hex2bin(k_vectors[1].sig_hex, sig, 64);
    hex2bin(k_vectors[1].msg_hex, msg, 1);
    /* Key from a different vector — a valid key, but not the signer's. */
    hex2bin(k_vectors[2].pubkey_hex, pk, 32);

    ASSERT(eos_ed25519_verify(sig, pk, msg, 1) != EOS_OK);
}

TEST(test_ed25519_malleated_signature_rejected)
{
    /*
     * S + L encodes the same signature a second way. RFC 8032 5.1.7 requires
     * rejecting a non-canonical S; accepting it would let an attacker mint a
     * distinct-but-valid signature for an already-signed image.
     * L = 2^252 + 27742317777372353535851937790883648493, little-endian.
     */
    static const uint8_t L[32] = {
        0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
        0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
    };
    uint8_t pk[32], sig[64], msg[1];

    hex2bin(k_vectors[1].pubkey_hex, pk, 32);
    hex2bin(k_vectors[1].sig_hex, sig, 64);
    hex2bin(k_vectors[1].msg_hex, msg, 1);

    unsigned carry = 0;
    for (int i = 0; i < 32; i++) {
        unsigned s = (unsigned)sig[32 + i] + L[i] + carry;
        sig[32 + i] = (uint8_t)(s & 0xff);
        carry = s >> 8;
    }

    ASSERT(eos_ed25519_verify(sig, pk, msg, 1) != EOS_OK);
}

/* ---- structural / defensive tests ---- */

TEST(test_ed25519_null_args)
{
    uint8_t pk[32] = {0}, sig[64] = {0}, msg[4] = {0};

    ASSERT(eos_ed25519_verify(NULL, pk, msg, sizeof(msg)) != EOS_OK);
    ASSERT(eos_ed25519_verify(sig, NULL, msg, sizeof(msg)) != EOS_OK);
    ASSERT(eos_ed25519_verify(sig, pk, NULL, sizeof(msg)) != EOS_OK);
}

TEST(test_ed25519_identity_key_forgery_rejected)
{
    /* The identity point encodes as 01 00..00. With the public key and R both
     * set to it and S zero, every term of the verification equation collapses
     * to the identity, so the equation holds for any message at all. That is
     * not a weakened signature; it is no signature. */
    uint8_t identity_pub[32] = {1};
    uint8_t identity_sig[64] = {1};
    const uint8_t msg[] = "untrusted firmware";

    ASSERT(eos_ed25519_verify(identity_sig, identity_pub,
                              msg, sizeof(msg) - 1) != EOS_OK);
}

/* The eight canonical low-order point encodings.
 *
 * Every order was computed rather than copied: decoding each y, recovering x,
 * and repeatedly adding the point until it reached the identity gives
 * 1, 2, 4, 4, 8, 8, 8, 8 for the entries below in order. An earlier revision
 * of this array held only five of them -- it omitted y=0 with the sign bit
 * set and both sign-flipped order-8 encodings -- while its comment claimed to
 * hold "the eight". [L](-A) = -[L]A, so the guard rejects a sign variant
 * whether or not it is listed; the reason to list them is that this is the
 * regression record for a secure-boot bypass, and a claimed class has to be
 * the class it claims. */
static const uint8_t k_low_order[8][32] = {
    /* order 1: the identity, y = 1 */
    {0x01},
    /* order 2: y = -1 */
    {0xEC,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
     0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F},
    /* order 4: y = 0, sign bit clear */
    {0x00},
    /* order 4: y = 0, sign bit set -- the encoding the earlier array missed */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80},
    /* order 8 */
    {0x26,0xE8,0x95,0x8F,0xC2,0xB2,0x27,0xB0,0x45,0xC3,0xF4,0x89,0xF2,0xEF,0x98,0xF0,
     0xD5,0xDF,0xAC,0x05,0xD3,0xC6,0x33,0x39,0xB1,0x38,0x02,0x88,0x6D,0x53,0xFC,0x05},
    /* order 8 */
    {0xC7,0x17,0x6A,0x70,0x3D,0x4D,0xD8,0x4F,0xBA,0x3C,0x0B,0x76,0x0D,0x10,0x67,0x0F,
     0x2A,0x20,0x53,0xFA,0x2C,0x39,0xCC,0xC6,0x4E,0xC7,0xFD,0x77,0x92,0xAC,0x03,0x7A},
    /* order 8: sign flip of the first order-8 entry -- also missing before */
    {0x26,0xE8,0x95,0x8F,0xC2,0xB2,0x27,0xB0,0x45,0xC3,0xF4,0x89,0xF2,0xEF,0x98,0xF0,
     0xD5,0xDF,0xAC,0x05,0xD3,0xC6,0x33,0x39,0xB1,0x38,0x02,0x88,0x6D,0x53,0xFC,0x85},
    /* order 8: sign flip of the second -- also missing before */
    {0xC7,0x17,0x6A,0x70,0x3D,0x4D,0xD8,0x4F,0xBA,0x3C,0x0B,0x76,0x0D,0x10,0x67,0x0F,
     0x2A,0x20,0x53,0xFA,0x2C,0x39,0xCC,0xC6,0x4E,0xC7,0xFD,0x77,0x92,0xAC,0x03,0xFA},
};

/* Not low-order points, and refused earlier and by a different mechanism:
 * unpackneg() rejects them on canonicality or because no x exists. Kept
 * separate so the array above means what its name says -- an earlier revision
 * spent one of its eight slots on D9FF..FF, which does not decode at all. */
static const uint8_t k_non_canonical[3][32] = {
    /* y = p: reduces to 0, decodes as an order-4 point but is not canonical */
    {0xED,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
     0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F},
    /* y = p + 1: reduces to the identity, likewise not canonical */
    {0xEE,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
     0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F},
    /* no x satisfies the curve equation for this y: unpackneg() fails */
    {0xD9,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
     0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
};

/* A low-order key forges for roughly one message in n, where n is its order,
 * so a single fixed message would let a real bypass pass this suite. */
static const char *const messages[] = {
    "untrusted firmware", "v1.0.0", "", "a", "boot", "eos", "1234", "payload",
};

TEST(test_ed25519_low_order_keys_rejected)
{
    /* zero_pubkey covers one encoding; Ed25519 has eight low-order points and
     * the family is what matters. A subgroup test alone is not enough either:
     * the identity has order 1, which divides L, so [L]identity = identity and
     * it passes. Both checks are required.
     *
     * The sweep is every low-order encoding as the key against every one as R,
     * over eight messages, because a low-order key of order n forges for
     * roughly one message in n -- a single fixed message would let a genuine
     * bypass through this test. Measured against 13a7a02, the last commit
     * before the subgroup check: 16 of the 64 (key, R) pairs were accepted by
     * at least one message. Here: none. */
    for (size_t k = 0; k < sizeof(k_low_order) / sizeof(k_low_order[0]); k++) {
        for (size_t r = 0; r < sizeof(k_low_order) / sizeof(k_low_order[0]); r++) {
            for (size_t m = 0; m < sizeof(messages) / sizeof(messages[0]); m++) {
                uint8_t sig[64];
                memset(sig, 0, sizeof(sig));
                memcpy(sig, k_low_order[r], 32);
                ASSERT(eos_ed25519_verify(sig, k_low_order[k],
                                          (const uint8_t *)messages[m],
                                          strlen(messages[m])) != EOS_OK);
            }
        }
    }
}

TEST(test_ed25519_non_canonical_encodings_rejected)
{
    /* These are refused before the subgroup check ever runs -- unpackneg()
     * rejects them on canonicality, or because no x satisfies the curve
     * equation. Pinned separately so that nobody deletes that path on the
     * grounds that the subgroup test now covers it. It does not. */
    for (size_t k = 0; k < sizeof(k_non_canonical) / sizeof(k_non_canonical[0]); k++) {
        for (size_t m = 0; m < sizeof(messages) / sizeof(messages[0]); m++) {
            uint8_t sig[64];
            memset(sig, 0, sizeof(sig));
            memcpy(sig, k_non_canonical[k], 32);
            ASSERT(eos_ed25519_verify(sig, k_non_canonical[k],
                                      (const uint8_t *)messages[m],
                                      strlen(messages[m])) != EOS_OK);
        }
    }
}

TEST(test_ed25519_zero_pubkey_rejected)
{
    uint8_t zero_pub[32], sig[64], msg[1];

    memset(zero_pub, 0, sizeof(zero_pub));
    hex2bin(k_vectors[1].sig_hex, sig, 64);
    hex2bin(k_vectors[1].msg_hex, msg, 1);

    ASSERT(eos_ed25519_verify(sig, zero_pub, msg, 1) != EOS_OK);
}

TEST(test_ed25519_zero_signature_rejected)
{
    uint8_t pk[32], sig[64], msg[1];

    hex2bin(k_vectors[1].pubkey_hex, pk, 32);
    hex2bin(k_vectors[1].msg_hex, msg, 1);
    memset(sig, 0, sizeof(sig));

    ASSERT(eos_ed25519_verify(sig, pk, msg, 1) != EOS_OK);
}

TEST(test_ed25519_low_order_R_with_a_valid_key_is_not_a_forgery)
{
    /* The subgroup check guards the public key, not R, and that is
     * deliberate rather than an omission. Verification computes
     * [S]B = R + [k]A: a low-order A collapses the equation for any message,
     * which is the bypass. A low-order R with a genuine A does not, because
     * k = SHA-512(R || A || M) depends on R, so an attacker choosing R would
     * have to solve for it.
     *
     * Measured rather than argued: all eight low-order encodings as R with
     * S = 0, against RFC 8032 test 2's real public key, are rejected on the
     * unfixed verifier too -- 0 of 8, before and after. Pinned here so that
     * a later "R should be checked as well" change is recognised as scope
     * creep, and so that removing the key check because "R is the untrusted
     * half" fails loudly.
     */
    uint8_t pk[32], sig[64];
    size_t r, m;

    hex2bin(k_vectors[1].pubkey_hex, pk, 32);

    for (r = 0; r < sizeof(k_low_order) / sizeof(k_low_order[0]); r++) {
        for (m = 0; m < sizeof(messages) / sizeof(messages[0]); m++) {
            memset(sig, 0, sizeof(sig));
            memcpy(sig, k_low_order[r], 32);
            ASSERT(eos_ed25519_verify(sig, pk,
                                      (const uint8_t *)messages[m],
                                      strlen(messages[m])) != EOS_OK);
        }
    }
}

TEST(test_sha512_known_answers)
{
    struct { const char *msg; const char *digest; } kat[] = {
        { "",
          "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
          "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e" },
        { "abc",
          "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
          "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f" },
    };

    for (size_t i = 0; i < sizeof(kat) / sizeof(kat[0]); i++) {
        uint8_t want[64], got[64];
        hex2bin(kat[i].digest, want, 64);
        eos_sha512((const uint8_t *)kat[i].msg, strlen(kat[i].msg), got);
        ASSERT(memcmp(got, want, 64) == 0);
    }
}

TEST(test_sha512_streaming_matches_one_shot)
{
    /* Chunking must not change the digest, including across the 128-byte
     * block boundary and the length-padding boundary at 112 bytes. */
    uint8_t buf[300];
    for (size_t i = 0; i < sizeof(buf); i++) buf[i] = (uint8_t)(i * 7 + 3);

    const size_t splits[] = { 1, 63, 111, 112, 113, 127, 128, 129, 255 };

    uint8_t one_shot[64];
    eos_sha512(buf, sizeof(buf), one_shot);

    for (size_t s = 0; s < sizeof(splits) / sizeof(splits[0]); s++) {
        size_t chunk = splits[s];
        eos_sha512_ctx_t ctx;
        uint8_t streamed[64];

        eos_sha512_init(&ctx);
        for (size_t off = 0; off < sizeof(buf); off += chunk) {
            size_t n = sizeof(buf) - off;
            if (n > chunk) n = chunk;
            eos_sha512_update(&ctx, buf + off, n);
        }
        eos_sha512_final(&ctx, streamed);

        ASSERT(memcmp(streamed, one_shot, 64) == 0);
    }
}

int main(void)
{
    printf("=== eBootloader: Ed25519 Signature Verification Tests ===\n\n");

    run_test_ed25519_rfc8032_vectors_accepted();
    run_test_ed25519_tampered_message_rejected();
    run_test_ed25519_every_signature_bit_flip_rejected();
    run_test_ed25519_wrong_public_key_rejected();
    run_test_ed25519_malleated_signature_rejected();
    run_test_ed25519_null_args();
    run_test_ed25519_identity_key_forgery_rejected();
    run_test_ed25519_low_order_keys_rejected();
    run_test_ed25519_non_canonical_encodings_rejected();
    run_test_ed25519_low_order_R_with_a_valid_key_is_not_a_forgery();
    run_test_ed25519_zero_pubkey_rejected();
    run_test_ed25519_zero_signature_rejected();
    run_test_sha512_known_answers();
    run_test_sha512_streaming_matches_one_shot();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
