// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file test_ed25519_contract.c
 * @brief eBoot's half of the Ed25519 contract shared with eos.
 *
 * eos and eBoot each carry their own Ed25519 verifier. They are different
 * implementations with opposite return conventions -- eos returns 1 to accept,
 * eBoot returns EOS_OK -- doing the same job on the same wire format, and for a
 * while only eos rejected low-order public keys. Nothing in either repo could
 * notice: there is no shared build, and the two are far too different for a
 * source diff to mean anything.
 *
 * tests/vectors/ed25519_contract_vectors.h is the part that can be shared: pure
 * data, byte-identical in both repos, with a digest over it. Each side runs its
 * own verifier against every vector and asserts the same digest. A change to one
 * copy that does not reach the other shows up as two different digests.
 *
 * The eos-side twin is tests/test_ed25519_contract.c in embeddedos-org/eos.
 * Both files pin the same digest below and recompute it over the vector bytes
 * at run time, so either an edited vector or a changed generator fails on
 * whichever side it lands.
 */

#include "eos_crypto_boot.h"
#include "eos_types.h"

#include "vectors/ed25519_contract_vectors.h"

#include <stdio.h>
#include <string.h>

/* The corpus digest, pinned in committed source.
 *
 * EOS_ED25519_CONTRACT_DIGEST comes from the generated header, which this
 * repo's own copy of tools/gen_ed25519_contract_vectors.py just wrote -- a
 * hash the generator took over its own output. Comparing it against nothing
 * made the drift guard unfalsifiable: edit the generator here and not in eos
 * and this repo regenerates a self-consistent corpus with a new digest, the
 * test still exits 0, and the divergence is visible only to a human reading
 * two CI logs in two repositories.
 *
 * Pinning it here, in a file a reviewer can diff, turns a generator change
 * into a failing test -- and the fix is a one-line edit that has to be made
 * identically in both repos, which is the coordination this corpus exists to
 * force.
 *
 * The count and accept-count are pinned for the same reason: both are
 * generated, so a corpus that silently shrank would otherwise still pass.
 */
#define EOS_ED25519_CONTRACT_EXPECTED \
    "1059febedae2b3e3dfaa1ef5b419fb37ed7f0d53b377a3250b53b95a546282a2"
#define EOS_ED25519_CONTRACT_EXPECTED_COUNT     76
#define EOS_ED25519_CONTRACT_EXPECTED_ACCEPTS   3

/* SHA-256 over the corpus, in the generator's serialisation order. */
static void contract_digest(char out_hex[65])
{
    eos_sha256_ctx_t sha;
    uint8_t digest[32];
    int i, j;

    eos_sha256_init(&sha);
    for (i = 0; i < EOS_ED25519_CONTRACT_COUNT; i++) {
        const eos_ed25519_contract_vector_t *v =
            &eos_ed25519_contract_vectors[i];
        uint8_t accept = (uint8_t)v->expect_accept;

        eos_sha256_update(&sha, v->public_key, sizeof v->public_key);
        eos_sha256_update(&sha, v->signature, sizeof v->signature);
        eos_sha256_update(&sha, v->message, v->message_len);
        eos_sha256_update(&sha, &accept, 1);
    }
    eos_sha256_final(&sha, digest);

    for (j = 0; j < 32; j++)
        snprintf(out_hex + j * 2, 3, "%02x", digest[j]);
    out_hex[64] = '\0';
}

int main(void)
{
    unsigned failed = 0, accepted = 0, refused = 0;

    printf("Ed25519 contract vectors\n");
    printf("  digest: %s\n", EOS_ED25519_CONTRACT_DIGEST);
    printf("  count:  %d\n\n", EOS_ED25519_CONTRACT_COUNT);

    /* Recomputed over the vector bytes, not read from the header.
     *
     * The previous version compared EOS_ED25519_CONTRACT_DIGEST against
     * EOS_ED25519_CONTRACT_EXPECTED -- two string literals, one of them
     * written into the same generated header as the data. Nothing hashed the
     * vectors, so editing a committed vector changed the data and left the
     * digest it was compared against untouched, and the suite still reported
     * all 76 behaving as specified. The guard fired only when someone re-ran
     * the generator: the file it protects was the one file it could not see.
     *
     * Serialisation matches tools/gen_ed25519_contract_vectors.py: for each
     * vector in order, public_key[32] || signature[64] ||
     * message[message_len] || (uint8_t)expect_accept. */
    char computed[65];
    contract_digest(computed);

    if (strcmp(computed, EOS_ED25519_CONTRACT_EXPECTED) != 0) {
        printf("[FAIL] corpus digest changed\n"
               "       expected %s\n"
               "       got      %s\n"
               "       A vector was edited, the generator was changed, or the\n"
               "       two repos' copies have diverged. Re-derive, confirm eos\n"
               "       and eBoot agree, then update the pin in BOTH.\n",
               EOS_ED25519_CONTRACT_EXPECTED, computed);
        return 1;
    }

    /* The header's own claim must agree too. If it does not, the committed
     * corpus and the header that describes it came from different runs. */
    if (strcmp(computed, EOS_ED25519_CONTRACT_DIGEST) != 0) {
        printf("[FAIL] the header's digest (%s) does not describe the vectors "
               "it ships with (%s)\n",
               EOS_ED25519_CONTRACT_DIGEST, computed);
        return 1;
    }

    if (EOS_ED25519_CONTRACT_COUNT != EOS_ED25519_CONTRACT_EXPECTED_COUNT) {
        printf("[FAIL] corpus is %d vectors, expected %d\n",
               EOS_ED25519_CONTRACT_COUNT,
               EOS_ED25519_CONTRACT_EXPECTED_COUNT);
        return 1;
    }

    for (int i = 0; i < EOS_ED25519_CONTRACT_COUNT; i++) {
        const eos_ed25519_contract_vector_t *v = &eos_ed25519_contract_vectors[i];

        int rc = eos_ed25519_verify(v->signature, v->public_key,
                                    v->message, v->message_len);
        int got_accept = (rc == EOS_OK);

        if (got_accept) accepted++; else refused++;

        if (got_accept != v->expect_accept) {
            printf("  [FAIL] %s\n         expected %s, got %s\n         %s\n",
                   v->name,
                   v->expect_accept ? "ACCEPT" : "refuse",
                   got_accept ? "ACCEPT" : "refuse",
                   v->why);
            failed++;
        }
    }

    printf("  %u accepted, %u refused, %u wrong\n\n", accepted, refused, failed);

    if (failed) {
        printf("[FAIL] %u of %d contract vectors behaved incorrectly\n",
               failed, EOS_ED25519_CONTRACT_COUNT);
        return 1;
    }

    /* A verifier that refuses everything satisfies all 73 negative vectors, so
     * the three RFC 8032 signatures carry the whole weight of proving this
     * still accepts real signatures. Fail loudly if they are ever dropped. */
    unsigned positives = 0;
    for (int i = 0; i < EOS_ED25519_CONTRACT_COUNT; i++)
        if (eos_ed25519_contract_vectors[i].expect_accept) positives++;
    if (positives != EOS_ED25519_CONTRACT_EXPECTED_ACCEPTS) {
        printf("[FAIL] the corpus has %u accept vectors, expected %d; "
               "a verifier that refuses everything would otherwise pass\n",
               positives, EOS_ED25519_CONTRACT_EXPECTED_ACCEPTS);
        return 1;
    }

    printf("[PASS] all %d contract vectors behaved as specified (%u must accept)\n",
           EOS_ED25519_CONTRACT_COUNT, positives);
    return 0;
}
