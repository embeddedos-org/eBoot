// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_fdt_loader.c
 * @brief Unit tests for the FDT loader against malformed device trees
 *
 * core/fdt_loader.c parses a blob that comes out of flash, so every offset
 * and length in its header is attacker-controlled. The file was in no
 * CMakeLists, so none of this was ever compiled, let alone run.
 *
 * Each negative case below is a blob that eos_fdt_validate() used to accept
 * -- it checked only magic and version -- and that then drove
 * eos_fdt_get_prop() off the end of the allocation. The positive case is
 * here to keep the bounds checks from simply rejecting everything.
 */

#include "eos_fdt_loader.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_passed = 0;

#define ASSERT(condition)                                                     \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "[FAIL] %s:%d: %s\n",                             \
                    __FILE__, __LINE__, #condition);                          \
            exit(1);                                                          \
        }                                                                     \
    } while (0)

#define RUN(test)                                                             \
    do {                                                                      \
        test();                                                               \
        tests_passed++;                                                       \
        printf("[PASS] %s\n", #test);                                         \
    } while (0)

/* ---- DTB construction helpers ---------------------------------------- */

#define BLOB_MAX 512

typedef struct {
    unsigned char bytes[BLOB_MAX];
    uint32_t len;
} blob_t;

static void put_be32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

static void append_be32(blob_t *b, uint32_t v)
{
    ASSERT(b->len + 4 <= BLOB_MAX);
    put_be32(b->bytes + b->len, v);
    b->len += 4;
}

/* Append a NUL-terminated string padded to a 4-byte boundary. */
static void append_padded(blob_t *b, const char *s)
{
    uint32_t n = (uint32_t)strlen(s) + 1;
    ASSERT(b->len + ((n + 3) & ~3U) <= BLOB_MAX);
    memcpy(b->bytes + b->len, s, n);
    b->len += n;
    while (b->len & 3U) b->bytes[b->len++] = 0;
}

/* A minimal well-formed tree: / { bootargs = "ro"; chosen { }; }
 *
 * Layout is header, then the struct block, then the strings block, so the
 * offsets the header carries are the real ones.
 *
 * The property sits on the root and the tests query "/". Node paths below
 * the root do not resolve: _get_prop derives the depth to match at by
 * counting slashes, so "/" looks for depth 1 -- which is the root
 * itself, the node "chosen" being at depth 2. That is a separate defect in
 * the same never-compiled file and is not what these tests are about, so
 * they stay on the one path form the matcher handles.
 */
static void build_valid(blob_t *b, const char *value)
{
    memset(b, 0, sizeof(*b));
    b->len = sizeof(fdt_header_t);
    uint32_t off_struct = b->len;

    append_be32(b, FDT_BEGIN_NODE); append_padded(b, "");

    uint32_t vlen = (uint32_t)strlen(value) + 1;
    append_be32(b, FDT_PROP);
    append_be32(b, vlen);
    append_be32(b, 0);              /* nameoff: "bootargs" at strings[0] */
    append_padded(b, value);

    append_be32(b, FDT_BEGIN_NODE); append_padded(b, "chosen");
    append_be32(b, FDT_END_NODE);
    append_be32(b, FDT_END_NODE);
    append_be32(b, FDT_END);
    uint32_t size_struct = b->len - off_struct;

    uint32_t off_strings = b->len;
    append_padded(b, "bootargs");
    uint32_t size_strings = b->len - off_strings;

    fdt_header_t *h = (fdt_header_t *)b->bytes;
    put_be32((unsigned char *)&h->magic, FDT_MAGIC);
    put_be32((unsigned char *)&h->version, 17);
    put_be32((unsigned char *)&h->last_comp_version, 16);
    put_be32((unsigned char *)&h->totalsize, b->len);
    put_be32((unsigned char *)&h->off_dt_struct, off_struct);
    put_be32((unsigned char *)&h->size_dt_struct, size_struct);
    put_be32((unsigned char *)&h->off_dt_strings, off_strings);
    put_be32((unsigned char *)&h->size_dt_strings, size_strings);
}

/* / { soc { uart { reg = <value>; }; }; decoy { }; }
 *
 * "uart" sits at depth 3 under "soc", and a second node named "uart" is
 * placed under "decoy" so that matching by last component alone would find
 * the wrong one. */
static void build_nested(blob_t *b, const char *value, const char *decoy)
{
    memset(b, 0, sizeof(*b));
    b->len = sizeof(fdt_header_t);
    uint32_t off_struct = b->len;
    uint32_t vlen = (uint32_t)strlen(value) + 1;
    uint32_t dlen = (uint32_t)strlen(decoy) + 1;

    append_be32(b, FDT_BEGIN_NODE); append_padded(b, "");

    append_be32(b, FDT_BEGIN_NODE); append_padded(b, "decoy");
    append_be32(b, FDT_BEGIN_NODE); append_padded(b, "uart");
    append_be32(b, FDT_PROP); append_be32(b, dlen); append_be32(b, 0);
    append_padded(b, decoy);
    append_be32(b, FDT_END_NODE);
    append_be32(b, FDT_END_NODE);

    append_be32(b, FDT_BEGIN_NODE); append_padded(b, "soc");
    append_be32(b, FDT_BEGIN_NODE); append_padded(b, "uart");
    append_be32(b, FDT_PROP); append_be32(b, vlen); append_be32(b, 0);
    append_padded(b, value);
    append_be32(b, FDT_END_NODE);
    append_be32(b, FDT_END_NODE);

    append_be32(b, FDT_END_NODE);
    append_be32(b, FDT_END);
    uint32_t size_struct = b->len - off_struct;

    uint32_t off_strings = b->len;
    append_padded(b, "reg");
    uint32_t size_strings = b->len - off_strings;

    fdt_header_t *h = (fdt_header_t *)b->bytes;
    put_be32((unsigned char *)&h->magic, FDT_MAGIC);
    put_be32((unsigned char *)&h->version, 17);
    put_be32((unsigned char *)&h->last_comp_version, 16);
    put_be32((unsigned char *)&h->totalsize, b->len);
    put_be32((unsigned char *)&h->off_dt_struct, off_struct);
    put_be32((unsigned char *)&h->size_dt_struct, size_struct);
    put_be32((unsigned char *)&h->off_dt_strings, off_strings);
    put_be32((unsigned char *)&h->size_dt_strings, size_strings);
}

/* Like build_nested, but with the unit addresses a real device tree carries.
 * Exact-equality matching resolved nothing here, which is the case the PR's
 * own headline example describes and could not do. */
static void build_addressed(blob_t *b, const char *v0, const char *v1)
{
    memset(b, 0, sizeof(*b));
    b->len = sizeof(fdt_header_t);
    uint32_t off_struct = b->len;
    uint32_t l0 = (uint32_t)strlen(v0) + 1;
    uint32_t l1 = (uint32_t)strlen(v1) + 1;

    append_be32(b, FDT_BEGIN_NODE); append_padded(b, "");
    append_be32(b, FDT_BEGIN_NODE); append_padded(b, "soc");

    append_be32(b, FDT_BEGIN_NODE); append_padded(b, "uart@40011000");
    append_be32(b, FDT_PROP); append_be32(b, l0); append_be32(b, 0);
    append_padded(b, v0);
    append_be32(b, FDT_END_NODE);

    append_be32(b, FDT_BEGIN_NODE); append_padded(b, "uart@40004400");
    append_be32(b, FDT_PROP); append_be32(b, l1); append_be32(b, 0);
    append_padded(b, v1);
    append_be32(b, FDT_END_NODE);

    append_be32(b, FDT_END_NODE);
    append_be32(b, FDT_END_NODE);
    append_be32(b, FDT_END);
    uint32_t size_struct = b->len - off_struct;

    uint32_t off_strings = b->len;
    append_padded(b, "reg");
    uint32_t size_strings = b->len - off_strings;

    fdt_header_t *h = (fdt_header_t *)b->bytes;
    put_be32((unsigned char *)&h->magic, FDT_MAGIC);
    put_be32((unsigned char *)&h->version, 17);
    put_be32((unsigned char *)&h->last_comp_version, 16);
    put_be32((unsigned char *)&h->totalsize, b->len);
    put_be32((unsigned char *)&h->off_dt_struct, off_struct);
    put_be32((unsigned char *)&h->size_dt_struct, size_struct);
    put_be32((unsigned char *)&h->off_dt_strings, off_strings);
    put_be32((unsigned char *)&h->size_dt_strings, size_strings);
}

static void set_field(blob_t *b, size_t field_offset, uint32_t v)
{
    put_be32(b->bytes + field_offset, v);
}

#define FIELD(name) offsetof(fdt_header_t, name)

/* Run get_prop on a heap copy sized exactly to the blob, so an
 * out-of-bounds read lands in a sanitizer's redzone rather than in
 * whatever the test's own stack happens to hold. */
static int get_prop_exact(const blob_t *b, const char *node, const char *prop,
                          void *out, uint32_t *out_len)
{
    unsigned char *heap = malloc(b->len);
    ASSERT(heap != NULL);
    memcpy(heap, b->bytes, b->len);
    int rc = eos_fdt_get_prop(heap, b->len, node, prop, out, out_len);
    free(heap);
    return rc;
}

/* ---- Tests ------------------------------------------------------------ */

static void test_valid_tree_round_trips(void)
{
    blob_t b; build_valid(&b, "ro");
    ASSERT(eos_fdt_validate(b.bytes, b.len) == 0);

    char out[16]; uint32_t len = sizeof out;
    ASSERT(get_prop_exact(&b, "/", "bootargs", out, &len) == 0);
    ASSERT(len == 3);
    ASSERT(strcmp(out, "ro") == 0);
}

static void test_missing_property_is_reported(void)
{
    blob_t b; build_valid(&b, "ro");
    char out[16]; uint32_t len = sizeof out;
    ASSERT(get_prop_exact(&b, "/", "nonesuch", out, &len) != 0);
}

static void test_struct_offset_past_the_blob_is_rejected(void)
{
    blob_t b; build_valid(&b, "ro");
    set_field(&b, FIELD(off_dt_struct), 0x100000);

    ASSERT(eos_fdt_validate(b.bytes, b.len) != 0);
    char out[16]; uint32_t len = sizeof out;
    ASSERT(get_prop_exact(&b, "/", "bootargs", out, &len) != 0);
}

static void test_strings_offset_past_the_blob_is_rejected(void)
{
    blob_t b; build_valid(&b, "ro");
    set_field(&b, FIELD(off_dt_strings), 0x100000);

    ASSERT(eos_fdt_validate(b.bytes, b.len) != 0);
    char out[16]; uint32_t len = sizeof out;
    ASSERT(get_prop_exact(&b, "/", "bootargs", out, &len) != 0);
}

static void test_struct_size_running_past_the_blob_is_rejected(void)
{
    blob_t b; build_valid(&b, "ro");
    set_field(&b, FIELD(size_dt_struct), 0xFFFFFF00U);

    ASSERT(eos_fdt_validate(b.bytes, b.len) != 0);
    char out[16]; uint32_t len = sizeof out;
    ASSERT(get_prop_exact(&b, "/", "bootargs", out, &len) != 0);
}

static void test_totalsize_below_the_header_is_rejected(void)
{
    blob_t b; build_valid(&b, "ro");
    set_field(&b, FIELD(totalsize), 8);
    ASSERT(eos_fdt_validate(b.bytes, b.len) != 0);
}

static void test_property_length_past_the_struct_block_is_rejected(void)
{
    /* The write was clamped to the caller's buffer, so this never
     * overflowed buf -- it read past the blob and copied what followed it
     * out to the caller. */
    blob_t b; build_valid(&b, "ro");

    /* Find the FDT_PROP tag and enlarge its length field. */
    for (uint32_t i = sizeof(fdt_header_t); i + 8 <= b.len; i += 4) {
        if (b.bytes[i] == 0 && b.bytes[i+1] == 0 &&
            b.bytes[i+2] == 0 && b.bytes[i+3] == FDT_PROP) {
            set_field(&b, i + 4, 0xFFFF0000U);
            break;
        }
    }
    char out[16]; uint32_t len = sizeof out;
    ASSERT(get_prop_exact(&b, "/", "bootargs", out, &len) != 0);
}

static void test_property_nameoff_past_the_strings_block_is_rejected(void)
{
    blob_t b; build_valid(&b, "ro");
    for (uint32_t i = sizeof(fdt_header_t); i + 12 <= b.len; i += 4) {
        if (b.bytes[i] == 0 && b.bytes[i+1] == 0 &&
            b.bytes[i+2] == 0 && b.bytes[i+3] == FDT_PROP) {
            set_field(&b, i + 8, 0xFFFF0000U);   /* nameoff */
            break;
        }
    }
    char out[16]; uint32_t len = sizeof out;
    ASSERT(get_prop_exact(&b, "/", "bootargs", out, &len) != 0);
}

static void test_unterminated_node_name_is_rejected(void)
{
    /* A node name that runs to the end of the struct block with no NUL:
     * strlen() used to read straight past it. */
    blob_t b; memset(&b, 0, sizeof b);
    b.len = sizeof(fdt_header_t);
    uint32_t off_struct = b.len;
    append_be32(&b, FDT_BEGIN_NODE);
    for (int i = 0; i < 8; i++) b.bytes[b.len++] = 'A';
    uint32_t size_struct = b.len - off_struct;
    uint32_t off_strings = b.len;
    b.bytes[b.len++] = 0; b.len += 3;

    fdt_header_t *h = (fdt_header_t *)b.bytes;
    put_be32((unsigned char *)&h->magic, FDT_MAGIC);
    put_be32((unsigned char *)&h->version, 17);
    put_be32((unsigned char *)&h->totalsize, b.len);
    put_be32((unsigned char *)&h->off_dt_struct, off_struct);
    put_be32((unsigned char *)&h->size_dt_struct, size_struct);
    put_be32((unsigned char *)&h->off_dt_strings, off_strings);
    put_be32((unsigned char *)&h->size_dt_strings, 4);

    char out[16]; uint32_t len = sizeof out;
    ASSERT(get_prop_exact(&b, "/", "bootargs", out, &len) != 0);
}

static void test_null_arguments_are_rejected(void)
{
    blob_t b; build_valid(&b, "ro");
    char out[16]; uint32_t len = sizeof out;
    ASSERT(eos_fdt_validate(NULL, 0) != 0);
    ASSERT(eos_fdt_get_prop(NULL, b.len, "/", "bootargs", out, &len) != 0);
    ASSERT(eos_fdt_get_prop(b.bytes, b.len, NULL, "bootargs", out, &len) != 0);
    ASSERT(eos_fdt_get_prop(b.bytes, b.len, "/", NULL, out, &len) != 0);
    ASSERT(eos_fdt_get_prop(b.bytes, b.len, "/", "bootargs", NULL, &len) != 0);
    ASSERT(eos_fdt_get_prop(b.bytes, b.len, "/", "bootargs", out, NULL) != 0);
}

/* The mirror image of the offset tests above. Those inflate an offset and
 * leave totalsize honest; this leaves every offset internally consistent and
 * inflates totalsize itself. Nothing in the header contradicts anything else,
 * so a check that bounds the blob against its own totalsize has nothing to
 * catch -- the only thing that knows better is the caller's allocation. */
static void test_an_inflated_totalsize_is_rejected_when_the_length_is_known(void)
{
    blob_t b; build_valid(&b, "ro");
    uint32_t honest = b.len;
    set_field(&b, FIELD(totalsize), 0x100000);
    b.len = honest;   /* the allocation stays what it really was */

    char out[16]; uint32_t len = sizeof out;

    /* Told the truth about the buffer, the parser refuses it. */
    ASSERT(eos_fdt_validate(b.bytes, honest) != 0);
    ASSERT(get_prop_exact(&b, "/", "bootargs", out, &len) != 0);
}

/* A property that does not fit used to be copied short and reported as a
 * success, so a clipped bootargs was indistinguishable from a complete one. */
static void test_a_property_too_large_for_the_buffer_is_not_truncated(void)
{
    blob_t b; build_valid(&b, "root=/dev/mmcblk0p2 rw quiet");

    char out[8];
    uint32_t len = sizeof out;
    memset(out, 0xAA, sizeof out);

    ASSERT(get_prop_exact(&b, "/", "bootargs", out, &len) == -7);
    /* and it reports what the caller would need to allocate */
    ASSERT(len == sizeof "root=/dev/mmcblk0p2 rw quiet");
    /* nothing was written into the short buffer */
    ASSERT(out[0] == (char)0xAA);
}

static void test_a_property_that_exactly_fits_still_succeeds(void)
{
    blob_t b; build_valid(&b, "ro");
    char out[3]; uint32_t len = sizeof out;
    ASSERT(get_prop_exact(&b, "/", "bootargs", out, &len) == 0);
    ASSERT(len == 3);
    ASSERT(strcmp(out, "ro") == 0);
}

/* An unrecognised tag used to be skipped -- the walk resynchronised 4 bytes
 * on and a struct block of arbitrary bytes parsed to a clean "not found".
 * Bounded, but a TCB parser that walks garbage to completion is accepting
 * input it does not understand. Now only FDT_NOP passes. */
static void test_a_garbage_tag_is_refused_not_skipped(void)
{
    blob_t b;
    memset(&b, 0, sizeof(b));
    b.len = sizeof(fdt_header_t);
    uint32_t off_struct = b.len;
    append_be32(&b, FDT_BEGIN_NODE); append_padded(&b, "");
    append_be32(&b, 0xDEADBEEFU);          /* not a token */
    append_be32(&b, FDT_END_NODE);
    append_be32(&b, FDT_END);
    uint32_t size_struct = b.len - off_struct;
    uint32_t off_strings = b.len;
    append_padded(&b, "bootargs");
    uint32_t size_strings = b.len - off_strings;
    fdt_header_t *h = (fdt_header_t *)b.bytes;
    put_be32((unsigned char *)&h->magic, FDT_MAGIC);
    put_be32((unsigned char *)&h->version, 17);
    put_be32((unsigned char *)&h->last_comp_version, 16);
    put_be32((unsigned char *)&h->totalsize, b.len);
    put_be32((unsigned char *)&h->off_dt_struct, off_struct);
    put_be32((unsigned char *)&h->size_dt_struct, size_struct);
    put_be32((unsigned char *)&h->off_dt_strings, off_strings);
    put_be32((unsigned char *)&h->size_dt_strings, size_strings);

    char out[16]; uint32_t len = sizeof out;
    ASSERT(get_prop_exact(&b, "/", "bootargs", out, &len) == -6);
}

/* And the one legal unknown, FDT_NOP, is padding the spec allows anywhere
 * between tokens -- refusing it would reject real device trees. */
static void test_nop_padding_does_not_break_resolution(void)
{
    blob_t b;
    memset(&b, 0, sizeof(b));
    b.len = sizeof(fdt_header_t);
    uint32_t off_struct = b.len;
    append_be32(&b, FDT_NOP);
    append_be32(&b, FDT_BEGIN_NODE); append_padded(&b, "");
    append_be32(&b, FDT_NOP);
    append_be32(&b, FDT_PROP); append_be32(&b, 3); append_be32(&b, 0);
    append_padded(&b, "ro");
    append_be32(&b, FDT_NOP);
    append_be32(&b, FDT_END_NODE);
    append_be32(&b, FDT_END);
    uint32_t size_struct = b.len - off_struct;
    uint32_t off_strings = b.len;
    append_padded(&b, "bootargs");
    uint32_t size_strings = b.len - off_strings;
    fdt_header_t *h = (fdt_header_t *)b.bytes;
    put_be32((unsigned char *)&h->magic, FDT_MAGIC);
    put_be32((unsigned char *)&h->version, 17);
    put_be32((unsigned char *)&h->last_comp_version, 16);
    put_be32((unsigned char *)&h->totalsize, b.len);
    put_be32((unsigned char *)&h->off_dt_struct, off_struct);
    put_be32((unsigned char *)&h->size_dt_struct, size_struct);
    put_be32((unsigned char *)&h->off_dt_strings, off_strings);
    put_be32((unsigned char *)&h->size_dt_strings, size_strings);

    char out[16]; uint32_t len = sizeof out;
    ASSERT(get_prop_exact(&b, "/", "bootargs", out, &len) == 0);
    ASSERT(strcmp(out, "ro") == 0);
}

/* The blob arrives wherever it arrives -- fuzz input, a buffer inside a
 * larger message -- and nothing promises 4-byte alignment. Header fields go
 * through the same memcpy rule as the struct block now; this pins it by
 * parsing from an odd offset, which UBSan's alignment check turns into a
 * hard failure if a direct member load ever comes back. */
static void test_an_unaligned_blob_parses(void)
{
    blob_t b; build_valid(&b, "ro");

    unsigned char *heap = malloc(b.len + 1);
    ASSERT(heap != NULL);
    memcpy(heap + 1, b.bytes, b.len);

    char out[16]; uint32_t len = sizeof out;
    ASSERT(eos_fdt_validate(heap + 1, b.len) == 0);
    ASSERT(eos_fdt_get_prop(heap + 1, b.len, "/", "bootargs", out, &len) == 0);
    ASSERT(strcmp(out, "ro") == 0);
    free(heap);
}

static void test_a_node_below_the_root_resolves(void)
{
    /* "/chosen" used to look for depth 1 -- the root -- so no path below
     * the root ever resolved and only "/" worked.
     *
     * This assertion is also what proves the parent is matched, and that is
     * load-bearing rather than incidental: build_nested emits "decoy" before
     * "soc", so a matcher comparing only the last component would meet
     * /decoy/uart first and return "decoy-uart". Getting "soc-uart" here
     * means the ancestors were checked. */
    blob_t b; build_nested(&b, "soc-uart", "decoy-uart");
    char out[24]; uint32_t len = sizeof out;
    ASSERT(get_prop_exact(&b, "/soc/uart", "reg", out, &len) == 0);
    ASSERT(strcmp(out, "soc-uart") == 0);
}

static void test_each_uart_returns_its_own_value(void)
{
    /* Matching the last component alone would return decoy/uart's value
     * for /soc/uart, whichever the walk reached first. */
    blob_t b; build_nested(&b, "soc-uart", "decoy-uart");
    char out[24]; uint32_t len = sizeof out;
    ASSERT(get_prop_exact(&b, "/decoy/uart", "reg", out, &len) == 0);
    ASSERT(strcmp(out, "decoy-uart") == 0);
}

static void test_a_path_that_is_not_in_the_tree_is_not_found(void)
{
    blob_t b; build_nested(&b, "soc-uart", "decoy-uart");
    char out[24]; uint32_t len = sizeof out;
    ASSERT(get_prop_exact(&b, "/soc/spi", "reg", out, &len) != 0);
    len = sizeof out;
    ASSERT(get_prop_exact(&b, "/nosuch/uart", "reg", out, &len) != 0);
}

static void test_a_deeper_path_than_the_tree_is_not_found(void)
{
    blob_t b; build_nested(&b, "soc-uart", "decoy-uart");
    char out[24]; uint32_t len = sizeof out;
    ASSERT(get_prop_exact(&b, "/soc/uart/child", "reg", out, &len) != 0);
}

/* The property arm used to test in_target alone, and in_target is only
 * cleared by the target's own END_NODE -- so it stayed true through the whole
 * subtree and a child's property came back as the parent's. /soc has no "reg"
 * of its own; the correct answer is "not found", and what came back was
 * /soc/uart's value. That is the same class of mistake this PR is named for:
 * reading the wrong node's registers. */
static void test_a_property_on_a_child_is_not_returned_as_the_parents(void)
{
    blob_t b; build_nested(&b, "soc-uart", "decoy-uart");
    char out[24]; uint32_t len = sizeof out;
    ASSERT(get_prop_exact(&b, "/soc", "reg", out, &len) != 0);

    /* and the child itself still resolves, so the guard is not just
     * refusing everything below the root */
    len = sizeof out;
    ASSERT(get_prop_exact(&b, "/soc/uart", "reg", out, &len) == 0);
    ASSERT(strcmp(out, "soc-uart") == 0);
}

/* A path deeper than FDT_MAX_PATH_DEPTH is input, not a programming error,
 * and used to share -1 with "a caller passed NULL". */
static void test_an_overlong_path_is_distinguishable_from_a_null_argument(void)
{
    blob_t b; build_nested(&b, "soc-uart", "decoy-uart");
    char out[24]; uint32_t len = sizeof out;

    char deep[128] = "";
    for (int i = 0; i < 20; i++) strcat(deep, "/a");

    ASSERT(get_prop_exact(&b, deep, "reg", out, &len) == -8);
    len = sizeof out;
    ASSERT(eos_fdt_get_prop(NULL, b.len, "/soc", "reg", out, &len) == -1);
}

/* The headline case: a path written without a unit address, against a tree
 * that has one. Exact string equality never resolved this, so /soc/uart did
 * not work on any real DTB -- only on trees built without addresses, which is
 * what every other fixture in this file constructs. */
static void test_a_path_without_a_unit_address_resolves_a_node_with_one(void)
{
    blob_t b; build_addressed(&b, "uart0", "uart1");
    char out[24]; uint32_t len = sizeof out;
    ASSERT(get_prop_exact(&b, "/soc/uart", "reg", out, &len) == 0);
    /* The first match wins, as it does for any other duplicate name. */
    ASSERT(strcmp(out, "uart0") == 0);
}

/* And an explicit address still selects exactly the node asked for, which is
 * what makes the loose match safe on a tree with several of a peripheral. */
static void test_an_explicit_unit_address_selects_that_node(void)
{
    blob_t b; build_addressed(&b, "uart0", "uart1");
    char out[24]; uint32_t len = sizeof out;

    ASSERT(get_prop_exact(&b, "/soc/uart@40004400", "reg", out, &len) == 0);
    ASSERT(strcmp(out, "uart1") == 0);

    len = sizeof out;
    ASSERT(get_prop_exact(&b, "/soc/uart@40011000", "reg", out, &len) == 0);
    ASSERT(strcmp(out, "uart0") == 0);

    /* An address that is not in the tree must not fall back to a loose match. */
    len = sizeof out;
    ASSERT(get_prop_exact(&b, "/soc/uart@deadbeef", "reg", out, &len) != 0);
}

int main(void)
{
    printf("=== eBootloader FDT Loader Tests ===\n");
    RUN(test_valid_tree_round_trips);
    RUN(test_missing_property_is_reported);
    RUN(test_struct_offset_past_the_blob_is_rejected);
    RUN(test_strings_offset_past_the_blob_is_rejected);
    RUN(test_struct_size_running_past_the_blob_is_rejected);
    RUN(test_totalsize_below_the_header_is_rejected);
    RUN(test_property_length_past_the_struct_block_is_rejected);
    RUN(test_property_nameoff_past_the_strings_block_is_rejected);
    RUN(test_unterminated_node_name_is_rejected);
    RUN(test_null_arguments_are_rejected);
    RUN(test_an_inflated_totalsize_is_rejected_when_the_length_is_known);
    RUN(test_a_property_too_large_for_the_buffer_is_not_truncated);
    RUN(test_a_property_that_exactly_fits_still_succeeds);
    RUN(test_a_garbage_tag_is_refused_not_skipped);
    RUN(test_nop_padding_does_not_break_resolution);
    RUN(test_an_unaligned_blob_parses);
    RUN(test_a_node_below_the_root_resolves);
    RUN(test_each_uart_returns_its_own_value);
    RUN(test_a_path_that_is_not_in_the_tree_is_not_found);
    RUN(test_a_deeper_path_than_the_tree_is_not_found);
    RUN(test_a_property_on_a_child_is_not_returned_as_the_parents);
    RUN(test_an_overlong_path_is_distinguishable_from_a_null_argument);
    RUN(test_a_path_without_a_unit_address_resolves_a_node_with_one);
    RUN(test_an_explicit_unit_address_selects_that_node);
    printf("\n%d tests passed\n", tests_passed);
    return 0;
}
