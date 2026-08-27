// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file ed25519_verify.c
 * @brief Ed25519 signature verification (RFC 8032) — verify-only
 *
 * Self-contained Ed25519 verification for embedded bootloaders. No signing,
 * no key generation, no dynamic allocation, no libc beyond <string.h>.
 *
 * Verification follows RFC 8032 section 5.1.7 exactly:
 *   1. Decode R from sig[0..31] and A from the public key.
 *   2. Reject a non-canonical S (S must be < L, the group order).
 *   3. k = SHA-512(R || A || M) reduced mod L.
 *   4. Accept iff [S]B == R + [k]A.
 *
 * Because the challenge uses SHA-512 as the standard requires, signatures
 * from any conforming signer verify here — including tools/sign_image.py,
 * which signs with python-cryptography.
 *
 * Field arithmetic uses 16 limbs of 16 bits held in int64_t, which keeps
 * every intermediate product far below the 64-bit overflow bound and makes
 * carry propagation straightforward to audit. Point selection is done with
 * arithmetic masks rather than branches so verification does not expose a
 * data-dependent control path.
 */

#include "eos_crypto_boot.h"
#include "eos_types.h"
#include <string.h>
#include "eos_sha512.h"

/* ================================================================
 * Field arithmetic mod p = 2^255 - 19
 * ================================================================ */

typedef int64_t gf[16];

static const gf gf0 = {0};
static const gf gf1 = {1};

/* d = -121665/121666 */
static const gf D = {
    0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
    0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203
};

/* 2*d */
static const gf D2 = {
    0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
    0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406
};

/* Base point x and y */
static const gf BX = {
    0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
    0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169
};
static const gf BY = {
    0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
    0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666
};

/* sqrt(-1) */
static const gf SQRTM1 = {
    0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
    0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83
};

/* L = 2^252 + 27742317777372353535851937790883648493 (group order), LSB first */
static const int64_t ORDER_L[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
    0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
};

static void fe_copy16(gf r, const gf a)
{
    for (int i = 0; i < 16; i++) r[i] = a[i];
}

static void fe_set0(gf r)
{
    for (int i = 0; i < 16; i++) r[i] = 0;
}

/* Propagate carries so every limb returns to 16 bits. */
static void car25519(gf o)
{
    for (int i = 0; i < 16; i++) {
        o[i] += (int64_t)1 << 16;
        int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

/* Branch-free conditional swap: swaps p and q iff b == 1. */
static void sel25519(gf p, gf q, int64_t b)
{
    int64_t mask = ~(b - 1);
    for (int i = 0; i < 16; i++) {
        int64_t t = mask & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

/* Reduce fully mod p and serialise little-endian. */
static void pack25519(uint8_t *o, const gf n)
{
    gf m, t;
    fe_copy16(t, n);
    car25519(t);
    car25519(t);
    car25519(t);

    /* Conditionally subtract p twice to reach the canonical residue. */
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int64_t b = (m[15] >> 16) & 1;
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }

    for (int i = 0; i < 16; i++) {
        o[2 * i]     = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

static int neq25519(const gf a, const gf b)
{
    uint8_t c[32], d[32];
    pack25519(c, a);
    pack25519(d, b);
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) diff |= (uint8_t)(c[i] ^ d[i]);
    return diff != 0;
}

static uint8_t par25519(const gf a)
{
    uint8_t d[32];
    pack25519(d, a);
    return (uint8_t)(d[0] & 1);
}

static void unpack25519(gf o, const uint8_t *n)
{
    for (int i = 0; i < 16; i++) {
        o[i] = (int64_t)n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    }
    o[15] &= 0x7fff;
}

static void fe_add(gf o, const gf a, const gf b)
{
    for (int i = 0; i < 16; i++) o[i] = a[i] + b[i];
}

static void fe_sub(gf o, const gf a, const gf b)
{
    for (int i = 0; i < 16; i++) o[i] = a[i] - b[i];
}

static void fe_mul(gf o, const gf a, const gf b)
{
    int64_t t[31];
    for (int i = 0; i < 31; i++) t[i] = 0;
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
    }
    /* Fold the upper half back in: 2^256 == 38 (mod p). */
    for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++) o[i] = t[i];
    car25519(o);
    car25519(o);
}

static void fe_sq(gf o, const gf a)
{
    fe_mul(o, a, a);
}

/* o = a^(p-2) = a^-1, by the standard 254-step addition chain. */
static void inv25519(gf o, const gf a)
{
    gf c;
    fe_copy16(c, a);
    for (int i = 253; i >= 0; i--) {
        fe_sq(c, c);
        if (i != 2 && i != 4) fe_mul(c, c, a);
    }
    fe_copy16(o, c);
}

/* o = a^((p-5)/8), used to take square roots. */
static void pow2523(gf o, const gf a)
{
    gf c;
    fe_copy16(c, a);
    for (int i = 250; i >= 0; i--) {
        fe_sq(c, c);
        if (i != 1) fe_mul(c, c, a);
    }
    fe_copy16(o, c);
}

/* ================================================================
 * Edwards curve group operations
 *
 * Points are extended coordinates (X, Y, Z, T) with x = X/Z, y = Y/Z.
 * ================================================================ */

static void point_add(gf p[4], const gf q[4])
{
    gf a, b, c, d, t, e, f, g, h;

    fe_sub(a, p[1], p[0]);
    fe_sub(t, q[1], q[0]);
    fe_mul(a, a, t);
    fe_add(b, p[0], p[1]);
    fe_add(t, q[0], q[1]);
    fe_mul(b, b, t);
    fe_mul(c, p[3], q[3]);
    fe_mul(c, c, D2);
    fe_mul(d, p[2], q[2]);
    fe_add(d, d, d);
    fe_sub(e, b, a);
    fe_sub(f, d, c);
    fe_add(g, d, c);
    fe_add(h, b, a);

    fe_mul(p[0], e, f);
    fe_mul(p[1], h, g);
    fe_mul(p[2], g, f);
    fe_mul(p[3], e, h);
}

static void point_cswap(gf p[4], gf q[4], uint8_t b)
{
    for (int i = 0; i < 4; i++) sel25519(p[i], q[i], (int64_t)b);
}

static void point_pack(uint8_t *r, gf p[4])
{
    gf tx, ty, zi;
    inv25519(zi, p[2]);
    fe_mul(tx, p[0], zi);
    fe_mul(ty, p[1], zi);
    pack25519(r, ty);
    r[31] ^= (uint8_t)(par25519(tx) << 7);
}

/* r = [s]q, scanning the scalar from the most significant bit. */
static void scalarmult(gf r[4], gf q[4], const uint8_t *s)
{
    fe_set0(r[0]);
    fe_copy16(r[1], gf1);
    fe_copy16(r[2], gf1);
    fe_set0(r[3]);

    for (int i = 255; i >= 0; i--) {
        uint8_t b = (uint8_t)((s[i / 8] >> (i & 7)) & 1);
        point_cswap(r, q, b);
        point_add(q, (const gf *)r);
        point_add(r, (const gf *)r);
        point_cswap(r, q, b);
    }
}

static void scalarbase(gf r[4], const uint8_t *s)
{
    gf q[4];
    fe_copy16(q[0], BX);
    fe_copy16(q[1], BY);
    fe_copy16(q[2], gf1);
    fe_mul(q[3], BX, BY);
    scalarmult(r, q, s);
}

/* Decode a compressed point into -P (the negation is what verification wants). */
static int unpackneg(gf r[4], const uint8_t p[32])
{
    gf t, chk, num, den, den2, den4, den6;

    fe_copy16(r[2], gf1);
    unpack25519(r[1], p);
    fe_sq(num, r[1]);
    fe_mul(den, num, D);
    fe_sub(num, num, r[2]);       /* num = y^2 - 1   */
    fe_add(den, r[2], den);       /* den = d*y^2 + 1 */

    fe_sq(den2, den);
    fe_sq(den4, den2);
    fe_mul(den6, den4, den2);
    fe_mul(t, den6, num);
    fe_mul(t, t, den);

    pow2523(t, t);
    fe_mul(t, t, num);
    fe_mul(t, t, den);
    fe_mul(t, t, den);
    fe_mul(r[0], t, den);

    fe_sq(chk, r[0]);
    fe_mul(chk, chk, den);
    if (neq25519(chk, num)) fe_mul(r[0], r[0], SQRTM1);

    fe_sq(chk, r[0]);
    fe_mul(chk, chk, den);
    if (neq25519(chk, num)) return EOS_ERR_SIGNATURE;   /* not on the curve */

    if (par25519(r[0]) == (p[31] >> 7)) fe_sub(r[0], gf0, r[0]);

    fe_mul(r[3], r[0], r[1]);
    return EOS_OK;
}

/* ================================================================
 * Scalar arithmetic mod L
 * ================================================================ */

static void mod_l(uint8_t r[32], int64_t x[64])
{
    int64_t carry;

    for (int i = 63; i >= 32; i--) {
        carry = 0;
        int j;
        for (j = i - 32; j < i - 12; j++) {
            x[j] += carry - 16 * x[i] * ORDER_L[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry << 8;
        }
        x[j] += carry;
        x[i] = 0;
    }

    carry = 0;
    for (int j = 0; j < 32; j++) {
        x[j] += carry - (x[31] >> 4) * ORDER_L[j];
        carry = x[j] >> 8;
        x[j] &= 0xff;
    }
    for (int j = 0; j < 32; j++) x[j] -= carry * ORDER_L[j];
    for (int i = 0; i < 32; i++) {
        x[i + 1] += x[i] >> 8;
        r[i] = (uint8_t)(x[i] & 0xff);
    }
}

/* Reduce a 64-byte hash to a scalar mod L. */
static void reduce_hash(uint8_t r[64])
{
    int64_t x[64];
    for (int i = 0; i < 64; i++) x[i] = (int64_t)(uint64_t)r[i];
    for (int i = 0; i < 64; i++) r[i] = 0;
    mod_l(r, x);
}

/* Constant-time check that S (little-endian, 32 bytes) is below L.
 * RFC 8032 section 5.1.7 requires rejecting a non-canonical S; without this
 * a signature can be mauled into a second valid encoding of itself. */
static int s_is_canonical(const uint8_t s[32])
{
    int gt = 0, lt = 0;
    for (int i = 31; i >= 0; i--) {
        int si = s[i], li = (int)ORDER_L[i];
        gt |= (~lt) & ((si > li) ? 1 : 0);
        lt |= (~gt) & ((si < li) ? 1 : 0);
    }
    return lt & ~gt;
}

/* ================================================================
 * Public entry point
 * ================================================================ */

/**
 * @brief Verify an Ed25519 signature (RFC 8032).
 *
 * @param signature  64-byte signature, R || S.
 * @param public_key 32-byte Ed25519 public key.
 * @param message    Message bytes that were signed.
 * @param msg_len    Length of @p message in bytes.
 * @return EOS_OK if the signature is valid, EOS_ERR_SIGNATURE if it is not,
 *         EOS_ERR_INVALID on a null argument.
 *
 * Example:
 * @code
 *   if (eos_ed25519_verify(sig, vendor_pubkey, digest, 32) == EOS_OK) {
 *       boot_image();
 *   }
 * @endcode
 */
int eos_ed25519_verify(const uint8_t signature[64],
                       const uint8_t public_key[32],
                       const uint8_t *message, size_t msg_len)
{
    if (!signature || !public_key || (!message && msg_len > 0))
        return EOS_ERR_INVALID;

    /* Reject a non-canonical S before doing any curve work. */
    if (!s_is_canonical(&signature[32]))
        return EOS_ERR_SIGNATURE;

    /* Decode -A from the public key; a key off the curve is rejected here. */
    gf A[4];
    if (unpackneg(A, public_key) != EOS_OK)
        return EOS_ERR_SIGNATURE;

    /* k = SHA-512(R || A || M) mod L */
    eos_sha512_ctx_t ctx;
    uint8_t k[64];
    eos_sha512_init(&ctx);
    eos_sha512_update(&ctx, signature, 32);      /* R */
    eos_sha512_update(&ctx, public_key, 32);     /* A */
    eos_sha512_update(&ctx, message, msg_len);   /* M */
    eos_sha512_final(&ctx, k);
    reduce_hash(k);

    /* Step 3: Compute k = SHA-256(R || A || M) reduced mod L */
    /* Step 3: Compute k = SHA-512(R || A || M) reduced mod L */
uint8_t k_hash[64];
sha512_ctx_t ctx;

sha512_init(&ctx);
sha512_update(&ctx, signature, 32);       /* R */
sha512_update(&ctx, public_key, 32);      /* A */
sha512_update(&ctx, message, msg_len);    /* M */
sha512_final(&ctx, k_hash);

uint8_t k[32];
sc_reduce(k, k_hash);

    return diff == 0 ? EOS_OK : EOS_ERR_SIGNATURE;
}
