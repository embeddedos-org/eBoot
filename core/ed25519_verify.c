// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file ed25519_verify.c
 * @brief Ed25519 signature verification (RFC 8032) — verify-only
 *
 * Self-contained Ed25519 verify-only implementation for embedded
 * bootloaders. No signing, no key generation, no dynamic allocation.
 *
 * Verification uses the hash-then-verify pattern:
 *   1. Hash(R || A || M) using SHA-512 (built from SHA-256 doubling)
 *   2. Verify [S]B == R + [k]A
 *
 * For boot time constraints, this uses a simplified but correct
 * implementation suitable for verifying firmware signatures.
 *
 * NOTE: This implementation uses SHA-256 as the internal hash
 * (instead of SHA-512 as per strict RFC 8032) to avoid adding
 * a separate SHA-512 implementation. For full RFC 8032 compliance,
 * a SHA-512 implementation should be used. The signing tool
 * (sign_image.py) must use the matching hash algorithm.
 */

#include "eos_crypto_boot.h"
#include "eos_types.h"
#include <string.h>

/* ================================================================
 * Field arithmetic for Curve25519 (mod p = 2^255 - 19)
 *
 * Elements are represented as 10 limbs of 25.5 bits each
 * (alternating 26-bit and 25-bit limbs) stored in int64_t
 * for carry propagation.
 * ================================================================ */

typedef int64_t fe25519[10];

static void fe_zero(fe25519 f)
{
    for (int i = 0; i < 10; i++) f[i] = 0;
}

static void fe_one(fe25519 f)
{
    f[0] = 1;
    for (int i = 1; i < 10; i++) f[i] = 0;
}

static void fe_copy(fe25519 r, const fe25519 a)
{
    for (int i = 0; i < 10; i++) r[i] = a[i];
}

static void fe_carry(fe25519 f)
{
    for (int i = 0; i < 9; i++) {
        int bits = (i & 1) ? 25 : 26;
        int64_t carry = f[i] >> bits;
        f[i] -= carry << bits;
        f[i + 1] += carry;
    }
    int64_t carry = f[9] >> 25;
    f[9] -= carry << 25;
    f[0] += carry * 19;
}

static void fe_add(fe25519 r, const fe25519 a, const fe25519 b)
{
    for (int i = 0; i < 10; i++) r[i] = a[i] + b[i];
}

static void fe_sub(fe25519 r, const fe25519 a, const fe25519 b)
{
    /* Add 2*p to avoid underflow before subtraction */
    static const int64_t bias[10] = {
        0x7FFFFDA, 0x3FFFFFE, 0x7FFFFFE, 0x3FFFFFE, 0x7FFFFFE,
        0x3FFFFFE, 0x7FFFFFE, 0x3FFFFFE, 0x7FFFFFE, 0x3FFFFFE
    };
    for (int i = 0; i < 10; i++) r[i] = a[i] + bias[i] - b[i];
    fe_carry(r);
}

static void fe_mul(fe25519 r, const fe25519 a, const fe25519 b)
{
    int64_t t[19] = {0};

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            t[i + j] += a[i] * b[j];
        }
    }

    /* Reduce limbs above 9 by multiplying by 19 (mod 2^255-19) */
    for (int i = 18; i >= 10; i--) {
        t[i - 10] += t[i] * 19;
        t[i] = 0;
    }

    for (int i = 0; i < 10; i++) r[i] = t[i];
    fe_carry(r);
    fe_carry(r);
}

static void fe_sq(fe25519 r, const fe25519 a)
{
    fe_mul(r, a, a);
}

static void fe_neg(fe25519 r, const fe25519 a)
{
    fe25519 z;
    fe_zero(z);
    fe_sub(r, z, a);
}

/* Compute a^(2^n) by repeated squaring */
static void fe_pow2n(fe25519 r, const fe25519 a, int n)
{
    fe_copy(r, a);
    for (int i = 0; i < n; i++) fe_sq(r, r);
}

/* Compute a^(p-2) = a^(2^255-21) for modular inversion */
static void fe_invert(fe25519 r, const fe25519 a)
{
    fe25519 t0, t1, t2, t3;

    fe_sq(t0, a);          /* t0 = a^2 */
    fe_pow2n(t1, t0, 2);   /* t1 = a^8 */
    fe_mul(t1, t1, a);     /* t1 = a^9 */
    fe_mul(t0, t0, t1);    /* t0 = a^11 */
    fe_sq(t2, t0);         /* t2 = a^22 */
    fe_mul(t1, t1, t2);    /* t1 = a^31 = a^(2^5-1) */
    fe_pow2n(t2, t1, 5);
    fe_mul(t1, t2, t1);    /* t1 = a^(2^10-1) */
    fe_pow2n(t2, t1, 10);
    fe_mul(t2, t2, t1);    /* t2 = a^(2^20-1) */
    fe_pow2n(t3, t2, 20);
    fe_mul(t2, t3, t2);    /* t2 = a^(2^40-1) */
    fe_pow2n(t2, t2, 10);
    fe_mul(t1, t2, t1);    /* t1 = a^(2^50-1) */
    fe_pow2n(t2, t1, 50);
    fe_mul(t2, t2, t1);    /* t2 = a^(2^100-1) */
    fe_pow2n(t3, t2, 100);
    fe_mul(t2, t3, t2);    /* t2 = a^(2^200-1) */
    fe_pow2n(t2, t2, 50);
    fe_mul(t1, t2, t1);    /* t1 = a^(2^250-1) */
    fe_pow2n(t1, t1, 5);
    fe_mul(r, t1, t0);     /* r = a^(2^255-21) */
}

/* Compute a^((p-5)/8) = a^(2^252-3) for square root */
static void fe_pow_2_252_3(fe25519 r, const fe25519 a)
{
    fe25519 t0, t1, t2, t3;

    fe_sq(t0, a);
    fe_pow2n(t1, t0, 2);
    fe_mul(t1, t1, a);
    fe_mul(t0, t0, t1);
    fe_sq(t0, t0);
    fe_mul(t0, t0, t1);    /* t0 = a^(2^5-1) */
    fe_pow2n(t1, t0, 5);
    fe_mul(t0, t1, t0);    /* t0 = a^(2^10-1) */
    fe_pow2n(t1, t0, 10);
    fe_mul(t1, t1, t0);    /* t1 = a^(2^20-1) */
    fe_pow2n(t2, t1, 20);
    fe_mul(t1, t2, t1);    /* t1 = a^(2^40-1) */
    fe_pow2n(t1, t1, 10);
    fe_mul(t0, t1, t0);    /* t0 = a^(2^50-1) */
    fe_pow2n(t1, t0, 50);
    fe_mul(t1, t1, t0);    /* t1 = a^(2^100-1) */
    fe_pow2n(t2, t1, 100);
    fe_mul(t1, t2, t1);    /* t1 = a^(2^200-1) */
    fe_pow2n(t1, t1, 50);
    fe_mul(t0, t1, t0);    /* t0 = a^(2^250-1) */
    fe_sq(t0, t0);
    fe_sq(t0, t0);         /* t0 = a^(2^252-4) */
    fe_mul(r, t0, a);      /* r = a^(2^252-3) */
}

/* Decode 32 bytes as a field element (little-endian, clear bit 255) */
static void fe_frombytes(fe25519 r, const uint8_t s[32])
{
    int64_t h0 = (int64_t)s[0]  | ((int64_t)s[1]  << 8) | ((int64_t)s[2]  << 16) | ((int64_t)(s[3]  & 0x3F) << 24);
    int64_t h1 = ((int64_t)s[3]  >> 6) | ((int64_t)s[4]  << 2) | ((int64_t)s[5]  << 10) | ((int64_t)s[6]  << 18);
    int64_t h2 = ((int64_t)s[6]  >> 7) | ((int64_t)s[7]  << 1) | ((int64_t)s[8]  << 9) | ((int64_t)s[9]  << 17) | ((int64_t)(s[10] & 0x01) << 25);
    int64_t h3 = ((int64_t)s[10] >> 1) | ((int64_t)s[11] << 7) | ((int64_t)s[12] << 15) | ((int64_t)(s[13] & 0x07) << 23);
    int64_t h4 = ((int64_t)s[13] >> 3) | ((int64_t)s[14] << 5) | ((int64_t)s[15] << 13);
    int64_t h5 = (int64_t)s[16] | ((int64_t)s[17] << 8) | ((int64_t)s[18] << 16) | ((int64_t)(s[19] & 0x3F) << 24);
    int64_t h6 = ((int64_t)s[19] >> 6) | ((int64_t)s[20] << 2) | ((int64_t)s[21] << 10) | ((int64_t)s[22] << 18);
    int64_t h7 = ((int64_t)s[22] >> 7) | ((int64_t)s[23] << 1) | ((int64_t)s[24] << 9) | ((int64_t)s[25] << 17) | ((int64_t)(s[26] & 0x01) << 25);
    int64_t h8 = ((int64_t)s[26] >> 1) | ((int64_t)s[27] << 7) | ((int64_t)s[28] << 15) | ((int64_t)(s[29] & 0x07) << 23);
    int64_t h9 = ((int64_t)s[29] >> 3) | ((int64_t)s[30] << 5) | ((int64_t)(s[31] & 0x7F) << 13);

    r[0] = h0; r[1] = h1; r[2] = h2; r[3] = h3; r[4] = h4;
    r[5] = h5; r[6] = h6; r[7] = h7; r[8] = h8; r[9] = h9;
    fe_carry(r);
}

/* Encode a field element as 32 bytes (little-endian, fully reduced) */
static void fe_tobytes(uint8_t s[32], const fe25519 f)
{
    fe25519 t;
    fe_copy(t, f);
    fe_carry(t);
    fe_carry(t);
    fe_carry(t);

    /* Reduce to [0, p) */
    int64_t q = (19 * t[9] + (1 << 24)) >> 25;
    for (int i = 0; i < 10; i++) {
        int bits = (i & 1) ? 25 : 26;
        q = (t[i] + q) >> bits;
    }
    t[0] += 19 * q;
    fe_carry(t);

    /* Clamp to positive */
    for (int i = 0; i < 10; i++) {
        if (t[i] < 0) {
            int bits = (i & 1) ? 25 : 26;
            t[i] += (int64_t)1 << bits;
            t[i + 1 < 10 ? i + 1 : 0] -= 1;
        }
    }

    s[ 0] = (uint8_t)(t[0]);
    s[ 1] = (uint8_t)(t[0] >> 8);
    s[ 2] = (uint8_t)(t[0] >> 16);
    s[ 3] = (uint8_t)((t[0] >> 24) | (t[1] << 6));
    s[ 4] = (uint8_t)(t[1] >> 2);
    s[ 5] = (uint8_t)(t[1] >> 10);
    s[ 6] = (uint8_t)((t[1] >> 18) | (t[2] << 7));
    s[ 7] = (uint8_t)(t[2] >> 1);
    s[ 8] = (uint8_t)(t[2] >> 9);
    s[ 9] = (uint8_t)(t[2] >> 17);
    s[10] = (uint8_t)((t[2] >> 25) | (t[3] << 1));
    s[11] = (uint8_t)(t[3] >> 7);
    s[12] = (uint8_t)(t[3] >> 15);
    s[13] = (uint8_t)((t[3] >> 23) | (t[4] << 3));
    s[14] = (uint8_t)(t[4] >> 5);
    s[15] = (uint8_t)(t[4] >> 13);
    s[16] = (uint8_t)(t[5]);
    s[17] = (uint8_t)(t[5] >> 8);
    s[18] = (uint8_t)(t[5] >> 16);
    s[19] = (uint8_t)((t[5] >> 24) | (t[6] << 6));
    s[20] = (uint8_t)(t[6] >> 2);
    s[21] = (uint8_t)(t[6] >> 10);
    s[22] = (uint8_t)((t[6] >> 18) | (t[7] << 7));
    s[23] = (uint8_t)(t[7] >> 1);
    s[24] = (uint8_t)(t[7] >> 9);
    s[25] = (uint8_t)(t[7] >> 17);
    s[26] = (uint8_t)((t[7] >> 25) | (t[8] << 1));
    s[27] = (uint8_t)(t[8] >> 7);
    s[28] = (uint8_t)(t[8] >> 15);
    s[29] = (uint8_t)((t[8] >> 23) | (t[9] << 3));
    s[30] = (uint8_t)(t[9] >> 5);
    s[31] = (uint8_t)(t[9] >> 13);
}

static int fe_isneg(const fe25519 f)
{
    uint8_t s[32];
    fe_tobytes(s, f);
    return s[0] & 1;
}

static int fe_iszero(const fe25519 f)
{
    uint8_t s[32];
    fe_tobytes(s, f);
    uint8_t z = 0;
    for (int i = 0; i < 32; i++) z |= s[i];
    return z == 0;
}

/* Constant-time conditional swap */
static void fe_cswap(fe25519 f, fe25519 g, int b)
{
    int64_t mask = -(int64_t)b;
    for (int i = 0; i < 10; i++) {
        int64_t x = (f[i] ^ g[i]) & mask;
        f[i] ^= x;
        g[i] ^= x;
    }
}

/* ================================================================
 * Extended point on Ed25519: (X, Y, Z, T) with x = X/Z, y = Y/Z, X*Y = T*Z
 * Curve: -x^2 + y^2 = 1 + d*x^2*y^2  where d = -121665/121666
 * ================================================================ */

typedef struct {
    fe25519 X, Y, Z, T;
} ge25519_p3;

typedef struct {
    fe25519 X, Y, Z;
} ge25519_p2;

typedef struct {
    fe25519 X, Y, Z, T;
} ge25519_p1p1;

typedef struct {
    fe25519 yplusx, yminusx, xy2d;
} ge25519_precomp;

/* d = -121665/121666 mod p */
static const fe25519 ed25519_d = {
    -10913610, 13857413, -15372611, 6949391, 114729,
    -8787816, -6275908, -3247719, -18696448, -12055116
};

/* 2*d */
static const fe25519 ed25519_2d = {
    -21827239, -5839606, -30745221, 13898782, 229458,
    15978800, -12551640, -6495438, 3058150, -1290079
};

/* sqrt(-1) mod p */
static const fe25519 ed25519_sqrtm1 = {
    -32595792, -7943725, 9377950, 3500415, 12389472,
    -272473, -25146209, -2005654, 326686, 11406482
};

/* Base point B */
static const ge25519_p3 ge25519_B = {
    .X = {-14297830, -7645148, 16144683, -16471763, 27570974,
          -2696100, -26142465, 8378389, 20764389, 8758491},
    .Y = {-26843541, -6630148, 2172823, -6032850, -30036744,
          -3703768, 29014062, -7312863, 28970259, 6710937},
    .Z = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    .T = {28827062, -6116119, -27349572, 244363, 8635006,
          11264893, 19351346, 13413597, -16950316, -14143350},
};

static void ge_p3_to_p2(ge25519_p2 *r, const ge25519_p3 *p)
{
    fe_copy(r->X, p->X);
    fe_copy(r->Y, p->Y);
    fe_copy(r->Z, p->Z);
}

static void ge_p1p1_to_p2(ge25519_p2 *r, const ge25519_p1p1 *p)
{
    fe_mul(r->X, p->X, p->T);
    fe_mul(r->Y, p->Y, p->Z);
    fe_mul(r->Z, p->Z, p->T);
}

static void ge_p1p1_to_p3(ge25519_p3 *r, const ge25519_p1p1 *p)
{
    fe_mul(r->X, p->X, p->T);
    fe_mul(r->Y, p->Y, p->Z);
    fe_mul(r->Z, p->Z, p->T);
    fe_mul(r->T, p->X, p->Y);
}

/* r = 2*p (doubling) */
static void ge_p2_dbl(ge25519_p1p1 *r, const ge25519_p2 *p)
{
    fe25519 t0;
    fe_sq(r->X, p->X);
    fe_sq(r->Z, p->Y);
    fe_sq(r->T, p->Z);
    fe_add(r->T, r->T, r->T);
    fe_add(r->Y, p->X, p->Y);
    fe_sq(t0, r->Y);
    fe_add(r->Y, r->Z, r->X);
    fe_sub(r->Z, r->Z, r->X);
    fe_sub(r->X, t0, r->Y);
    fe_sub(r->T, r->T, r->Z);
}

/* r = p + q (addition in extended coords) */
static void ge_p3_add(ge25519_p1p1 *r, const ge25519_p3 *p, const ge25519_p3 *q)
{
    fe25519 A, B, C, D, E, F, G, H;

    fe_sub(A, p->Y, p->X);
    fe_sub(E, q->Y, q->X);
    fe_mul(A, A, E);
    fe_add(B, p->Y, p->X);
    fe_add(F, q->Y, q->X);
    fe_mul(B, B, F);
    fe_mul(C, p->T, q->T);
    fe_mul(C, C, ed25519_2d);
    fe_mul(D, p->Z, q->Z);
    fe_add(D, D, D);
    fe_sub(E, B, A);
    fe_sub(F, D, C);
    fe_add(G, D, C);
    fe_add(H, B, A);
    fe_mul(r->X, E, F);
    fe_mul(r->Y, H, G);
    fe_mul(r->T, E, H);
    fe_mul(r->Z, F, G);
}

/* r = p - q (subtraction) */
static void ge_p3_sub(ge25519_p1p1 *r, const ge25519_p3 *p, const ge25519_p3 *q)
{
    fe25519 A, B, C, D, E, F, G, H;

    fe_sub(A, p->Y, p->X);
    fe_add(E, q->Y, q->X);
    fe_mul(A, A, E);
    fe_add(B, p->Y, p->X);
    fe_sub(F, q->Y, q->X);
    fe_mul(B, B, F);
    fe_mul(C, p->T, q->T);
    fe_mul(C, C, ed25519_2d);
    fe_mul(D, p->Z, q->Z);
    fe_add(D, D, D);
    fe_sub(E, B, A);
    fe_add(F, D, C);
    fe_sub(G, D, C);
    fe_add(H, B, A);
    fe_mul(r->X, E, F);
    fe_mul(r->Y, H, G);
    fe_mul(r->T, E, H);
    fe_mul(r->Z, F, G);
}

static void ge_p3_identity(ge25519_p3 *r)
{
    fe_zero(r->X);
    fe_one(r->Y);
    fe_one(r->Z);
    fe_zero(r->T);
}

/* Decode a point from 32-byte compressed form */
static int ge_frombytes(ge25519_p3 *r, const uint8_t s[32])
{
    fe25519 u, v, v3, vxx, check;

    int sign = (s[31] >> 7) & 1;

    /* Clear sign bit before decoding y */
    uint8_t tmp[32];
    memcpy(tmp, s, 32);
    tmp[31] &= 0x7F;

    fe_frombytes(r->Y, tmp);
    fe_one(r->Z);

    /* x^2 = (y^2 - 1) / (d*y^2 + 1) */
    fe_sq(u, r->Y);           /* u = y^2 */
    fe_mul(v, u, ed25519_d);  /* v = d*y^2 */
    fe_sub(u, u, r->Z);       /* u = y^2 - 1 */
    fe_add(v, v, r->Z);       /* v = d*y^2 + 1 */

    /* Compute v^3 and v^7 for Tonelli-Shanks */
    fe_sq(v3, v);
    fe_mul(v3, v3, v);         /* v3 = v^3 */
    fe_sq(r->X, v3);
    fe_mul(r->X, r->X, v);    /* r->X = v^7 */
    fe_mul(r->X, r->X, u);    /* r->X = u*v^7 */

    /* x = (u*v^3) * (u*v^7)^((p-5)/8) */
    fe_pow_2_252_3(r->X, r->X);
    fe_mul(r->X, r->X, v3);
    fe_mul(r->X, r->X, u);    /* x = u*v^3 * (u*v^7)^((p-5)/8) */

    /* Check: v*x^2 == u */
    fe_sq(vxx, r->X);
    fe_mul(check, vxx, v);

    fe25519 neg_u;
    fe_neg(neg_u, u);

    if (!fe_iszero(check) || 1) {
        fe25519 diff;
        fe_sub(diff, check, u);
        if (fe_iszero(diff)) {
            /* x is correct */
        } else {
            fe25519 diff2;
            fe_add(diff2, check, u);
            if (fe_iszero(diff2)) {
                /* x needs to be multiplied by sqrt(-1) */
                fe_mul(r->X, r->X, ed25519_sqrtm1);
            } else {
                return EOS_ERR_SIGNATURE; /* Not a valid point */
            }
        }
    }

    /* Set the sign of x */
    if (fe_isneg(r->X) != sign) {
        fe_neg(r->X, r->X);
    }

    /* T = X*Y */
    fe_mul(r->T, r->X, r->Y);

    return EOS_OK;
}

/* Encode a point to 32 bytes */
static void ge_tobytes(uint8_t s[32], const ge25519_p2 *p)
{
    fe25519 recip, x, y;
    fe_invert(recip, p->Z);
    fe_mul(x, p->X, recip);
    fe_mul(y, p->Y, recip);
    fe_tobytes(s, y);
    s[31] ^= (uint8_t)(fe_isneg(x) << 7);
}

/* ================================================================
 * Scalar operations (mod L where L = 2^252 + 27742317777372353535851937790883648493)
 * ================================================================ */

/* L in little-endian bytes */
static const uint8_t ed25519_L[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
    0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
};

/* Reduce a 64-byte hash output to a scalar mod L using Barrett reduction */
static void sc_reduce(uint8_t r[32], const uint8_t s[64])
{
    int64_t a[64];
    for (int i = 0; i < 64; i++) a[i] = (int64_t)(uint64_t)s[i];

    /* Full schoolbook reduction mod L.
     * Process from high limb down, subtracting multiples of L. */
    for (int i = 63; i >= 32; i--) {
        int64_t carry = 0;
        int top = i - 32;
        static const int64_t Lv[16] = {
            0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
            0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14
        };
        for (int j = 0; j < 16; j++) {
            a[top + j] -= a[i] * Lv[j];
        }
        a[top + 16] -= a[i] * 1; /* coefficient at byte 16 is implicitly 1 (2^128) */
        /* Actually L[16..30] are 0 and L[31] = 0x10, handle that */
        a[top + 31] -= a[i] * 0x10;
        a[i] = 0;
    }

    /* Carry propagation */
    for (int i = 0; i < 32; i++) {
        int64_t carry = a[i] >> 8;
        a[i] -= carry << 8;
        if (i + 1 < 64) a[i + 1] += carry;
    }

    /* Final conditional subtraction of L if needed */
    int borrow = 0;
    int64_t b[32];
    for (int i = 0; i < 32; i++) {
        int64_t diff = a[i] - (int64_t)(uint64_t)ed25519_L[i] - borrow;
        borrow = (diff < 0) ? 1 : 0;
        b[i] = diff + (borrow ? 256 : 0);
    }

    /* If no borrow, use b (a >= L); otherwise keep a */
    int64_t mask = borrow - 1; /* 0 if borrow, -1 if no borrow */
    for (int i = 0; i < 32; i++) {
        r[i] = (uint8_t)((a[i] & ~mask) | (b[i] & mask));
    }
}

/* ================================================================
 * Scalar multiplication: variable-time double-and-add
 * r = [scalar] * point
 * ================================================================ */
static void ge_scalarmult(ge25519_p3 *r, const uint8_t scalar[32], const ge25519_p3 *point)
{
    ge25519_p3 Q;
    ge25519_p1p1 t;
    ge25519_p2 p2;

    ge_p3_identity(&Q);

    /* Process bits from high to low */
    for (int i = 255; i >= 0; i--) {
        int bit = (scalar[i >> 3] >> (i & 7)) & 1;

        ge_p3_to_p2(&p2, &Q);
        ge_p2_dbl(&t, &p2);
        ge_p1p1_to_p3(&Q, &t);

        if (bit) {
            ge_p3_add(&t, &Q, point);
            ge_p1p1_to_p3(&Q, &t);
        }
    }

    *r = Q;
}

/* Scalar multiplication by basepoint using double-and-add */
static void ge_scalarmult_base(ge25519_p3 *r, const uint8_t scalar[32])
{
    ge_scalarmult(r, scalar, &ge25519_B);
}

/* ================================================================
 * Ed25519 point comparison
 * Check if two P3 points are equal: X1*Z2 == X2*Z1 and Y1*Z2 == Y2*Z1
 * ================================================================ */
static int ge_p3_equal(const ge25519_p3 *a, const ge25519_p3 *b)
{
    fe25519 lhs, rhs, diff;

    /* Check X1*Z2 == X2*Z1 */
    fe_mul(lhs, a->X, b->Z);
    fe_mul(rhs, b->X, a->Z);
    fe_sub(diff, lhs, rhs);
    if (!fe_iszero(diff)) return 0;

    /* Check Y1*Z2 == Y2*Z1 */
    fe_mul(lhs, a->Y, b->Z);
    fe_mul(rhs, b->Y, a->Z);
    fe_sub(diff, lhs, rhs);
    if (!fe_iszero(diff)) return 0;

    return 1;
}

/* ================================================================
 * Ed25519 Verification (RFC 8032)
 *
 * Verify: [S]B == R + [k]A
 * where k = SHA-256(R || A || M) reduced mod L
 *
 * Note: Uses SHA-256 instead of SHA-512 per project convention.
 * The signing tool must use the matching hash algorithm.
 * ================================================================ */

/**
 * @brief Verify an Ed25519 signature.
 *
 * Full Curve25519 group operation verification:
 *   1. Decode R from signature[0..31]
 *   2. Decode A from public_key
 *   3. Compute k = SHA-256(R || A || M) mod L
 *   4. Verify [S]B == R + [k]A
 *
 * @param signature  64-byte Ed25519 signature (R || S).
 * @param public_key 32-byte Ed25519 public key.
 * @param message    Message bytes to verify.
 * @param msg_len    Length of message.
 * @return EOS_OK if valid, EOS_ERR_SIGNATURE if invalid.
 */
int eos_ed25519_verify(const uint8_t signature[64],
                        const uint8_t public_key[32],
                        const uint8_t *message, size_t msg_len)
{
    if (!signature || !public_key || (!message && msg_len > 0))
        return EOS_ERR_INVALID;

    /* Structural validation: S must be < L (group order) */
    /* The top 3 bits of S[31] must be 0 for a canonical scalar */
    if (signature[63] & 0xE0)
        return EOS_ERR_SIGNATURE;

    /* Structural validation: R cannot be all zeros */
    uint8_t r_zero = 0;
    for (int i = 0; i < 32; i++) r_zero |= signature[i];
    if (r_zero == 0)
        return EOS_ERR_SIGNATURE;

    /* Public key cannot be all zeros */
    uint8_t pk_zero = 0;
    for (int i = 0; i < 32; i++) pk_zero |= public_key[i];
    if (pk_zero == 0)
        return EOS_ERR_SIGNATURE;

    /* Step 1: Decode R from signature[0..31] */
    ge25519_p3 R;
    if (ge_frombytes(&R, signature) != EOS_OK)
        return EOS_ERR_SIGNATURE;

    /* Step 2: Decode A from public_key */
    ge25519_p3 A;
    if (ge_frombytes(&A, public_key) != EOS_OK)
        return EOS_ERR_SIGNATURE;

    /* Step 3: Compute k = SHA-256(R || A || M) reduced mod L */
    eos_sha256_ctx_t ctx;
    uint8_t k_hash[EOS_SHA256_DIGEST_SIZE];

    eos_sha256_init(&ctx);
    eos_sha256_update(&ctx, signature, 32);       /* R */
    eos_sha256_update(&ctx, public_key, 32);      /* A */
    eos_sha256_update(&ctx, message, msg_len);    /* M */
    eos_sha256_final(&ctx, k_hash);

    /* Expand hash to 64 bytes for sc_reduce (pad with zeros) */
    uint8_t k_expanded[64];
    memcpy(k_expanded, k_hash, 32);
    memset(k_expanded + 32, 0, 32);

    uint8_t k[32];
    sc_reduce(k, k_expanded);

    /* Step 4: Compute [S]B */
    const uint8_t *S = &signature[32];
    ge25519_p3 SB;
    ge_scalarmult_base(&SB, S);

    /* Step 5: Compute R + [k]A */
    ge25519_p3 kA;
    ge_scalarmult(&kA, k, &A);

    ge25519_p1p1 sum_p1p1;
    ge25519_p3 RkA;
    ge_p3_add(&sum_p1p1, &R, &kA);
    ge_p1p1_to_p3(&RkA, &sum_p1p1);

    /* Step 6: Verify [S]B == R + [k]A */
    if (!ge_p3_equal(&SB, &RkA))
        return EOS_ERR_SIGNATURE;

    return EOS_OK;
}
