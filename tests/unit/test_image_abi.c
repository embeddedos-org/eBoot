/**
 * @file test_image_abi.c
 * @brief Pins the .efw image header wire format.
 *
 * eos_image_header_t is not merely an internal struct. eBoot parses images that
 * eFirmware writes, and the signing tools address these fields by absolute byte
 * offset. The layout and the constants carried inside it are a contract between
 * two repositories that cannot see each other at compile time.
 *
 * eos_image.h carries four _Static_asserts: sizeof, and the offsets of hash,
 * sig_type and signature. Those catch a field that grows or a field inserted
 * before hash. They do not catch two same-width fields exchanging places —
 * transposing load_addr and entry_addr moves neither the size nor any asserted
 * offset — and they say nothing at all about the *value* of EOS_IMG_MAGIC or of
 * the eos_sig_type_t enumerators, which travel inside the image and are wire
 * format just as much as the offsets are.
 *
 * Every number below is duplicated from eFirmware's tests/test_abi.c on purpose.
 * Two independent statements of the same contract, each in the repository it
 * governs, is the point: if one side is edited the other keeps the old value and
 * the build goes red.
 */

#include "eos_image.h"
#include "eos_types.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int checks_run = 0;
static int checks_failed = 0;

#define CHECK_EQ(actual, expect)                                              \
    do {                                                                      \
        unsigned long a_ = (unsigned long)(actual);                           \
        unsigned long e_ = (unsigned long)(expect);                           \
        checks_run++;                                                         \
        if (a_ != e_) {                                                       \
            checks_failed++;                                                  \
            printf("  [FAIL] %-44s got %lu, want %lu\n", #actual, a_, e_);    \
        }                                                                     \
    } while (0)

/* Offset and width together. Either alone lets drift through: a field can keep
 * its offset while changing width, and every following field then shifts. */
#define CHECK_FIELD(field, offset, width)                                     \
    do {                                                                      \
        CHECK_EQ(offsetof(eos_image_header_t, field), offset);                \
        CHECK_EQ(sizeof(((eos_image_header_t *)0)->field), width);            \
    } while (0)

static void section(const char *name)
{
    printf("  %s\n", name);
}

int main(void)
{
    printf("Image header ABI\n\n");

    section("field layout");
    CHECK_FIELD(magic,          0,   4);
    CHECK_FIELD(hdr_version,    4,   2);
    CHECK_FIELD(hdr_size,       6,   2);
    CHECK_FIELD(image_size,     8,   4);
    CHECK_FIELD(load_addr,      12,  4);
    CHECK_FIELD(entry_addr,     16,  4);
    CHECK_FIELD(image_version,  20,  4);
    CHECK_FIELD(flags,          24,  4);
    CHECK_FIELD(hash,           28,  32);
    CHECK_FIELD(sig_type,       60,  1);
    CHECK_FIELD(sig_len,        61,  1);
    CHECK_FIELD(reserved,       62,  30);
    CHECK_FIELD(signature,      92,  64);

    /* No padding anywhere, and none on the end. The struct is written to flash
     * and read back byte for byte; a compiler inserting a pad byte would shift
     * every field after it on one side of the link only. */
    CHECK_EQ(sizeof(eos_image_header_t), 156);

    section("constants carried inside the image");

    /* "EOSI" read as a big-endian word. eFirmware's EFW_IMAGE_MAGIC states the
     * same number. If these two ever disagree the bootloader rejects every
     * image the toolchain produces. */
    CHECK_EQ(EOS_IMG_MAGIC, 0x454F5349u);

    CHECK_EQ(EOS_HASH_SIZE, 32);
    CHECK_EQ(EOS_SIG_MAX_SIZE, 64);

    /* The signed prefix is everything but signature[] itself. Shrinking it
     * silently unauthenticates whichever fields fall outside — which is the
     * exact attack the v2 header format exists to close. */
    CHECK_EQ(EOS_IMG_SIGNED_LEN, 92);

    section("signature type enumerators");

    /* These are stored in sig_type as a single byte and interpreted by whoever
     * reads the image. Renumbering them makes an old image's signature be
     * checked under a different algorithm than the one that produced it. */
    CHECK_EQ(EOS_SIG_NONE,    0);
    CHECK_EQ(EOS_SIG_CRC32,   1);
    CHECK_EQ(EOS_SIG_SHA256,  2);
    CHECK_EQ(EOS_SIG_ED25519, 3);
    CHECK_EQ(EOS_SIG_ECDSA,   4);

    /* sig_type is one byte, so every enumerator has to fit in one. */
    CHECK_EQ(sizeof(((eos_image_header_t *)0)->sig_type), 1);
    CHECK_EQ(EOS_SIG_ECDSA <= 0xFF, 1);

    printf("\n%d/%d checks passed\n", checks_run - checks_failed, checks_run);
    return checks_failed == 0 ? 0 : 1;
}
