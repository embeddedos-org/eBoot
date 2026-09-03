// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project

/**
 * @file test_eos_sign_boot_path.c
 * @brief Run the real boot path over real eos_sign.py output.
 *
 * tools/eos_sign.py emits [header][TLV area][payload]. It used to stamp
 * hdr_size as a fixed 156 -- the struct size alone -- while
 * core/image_verify.c computes `payload_addr = addr + hdr->hdr_size`. That
 * landed on the TLV block, so the SHA-256 taken from there never matched
 * hash[] and every image the tool produced failed verification on-device.
 *
 * The Python-side test for this checks the tool's own arithmetic, which is
 * close to checking that the tool agrees with itself. This one stages the
 * tool's actual bytes into simulated flash and calls the bootloader's own
 * eos_image_parse_header() and eos_image_verify_integrity().
 *
 * The fixture carries both layouts over the same payload and key, so the fix
 * and the defect are asserted together: current output must verify, the old
 * output must be refused.
 */

#include "eos_image.h"
#include "eos_hal.h"
#include "eos_types.h"

#include "../vectors/signed_image_fixture.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define SIM_FLASH_SIZE (64 * 1024)
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
static void sim_noop(void) {}
static void sim_noop_u32(uint32_t v) { (void)v; }
static eos_reset_reason_t sim_reset_reason(void) { return EOS_RESET_POWER_ON; }
static void sim_system_reset(void) {}
static bool sim_recovery_pin(void) { return false; }
static void sim_jump(uint32_t a) { (void)a; }

static const eos_board_ops_t sim_ops = {
    .flash_base = 0, .flash_size = SIM_FLASH_SIZE,
    .slot_a_addr = 0x4000, .slot_a_size = 0x8000,
    .slot_b_addr = 0xC000, .slot_b_size = 0x8000,
    .recovery_addr = 0, .recovery_size = 0,
    .bootctl_addr = 0, .bootctl_backup_addr = 0x1000,
    .log_addr = 0x2000, .app_vector_offset = 0,
    .flash_read = sim_flash_read,
    .flash_write = sim_flash_write,
    .flash_erase = sim_flash_erase,
    .watchdog_init = sim_noop_u32,
    .watchdog_feed = sim_noop,
    .get_reset_reason = sim_reset_reason,
    .system_reset = sim_system_reset,
    .recovery_pin_asserted = sim_recovery_pin,
    .jump = sim_jump,
    .uart_init = NULL, .uart_send = NULL, .uart_recv = NULL,
};

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

#define IMAGE_ADDR 0x4000u

static int stage_and_verify(const unsigned char *img, size_t len,
                            eos_image_header_t *hdr_out)
{
    memset(sim_flash, 0xFF, sizeof sim_flash);
    memcpy(&sim_flash[IMAGE_ADDR], img, len);
    eos_hal_init(&sim_ops);

    if (eos_image_parse_header(IMAGE_ADDR, hdr_out) != EOS_OK)
        return EOS_ERR_INVALID;
    return eos_image_verify_integrity(hdr_out, IMAGE_ADDR);
}

int main(void)
{
    eos_image_header_t hdr;
    int rc;

    printf("=== eos_sign.py output through the real boot path ===\n\n");

    /* Current output: [header][payload][TLV], so addr + hdr_size is the
     * payload and the stored SHA-256 matches what the bootloader hashes. */
    memset(&hdr, 0, sizeof hdr);
    rc = stage_and_verify(eos_fixture_image_good, EOS_FIXTURE_GOOD_LEN, &hdr);
    CHECK(rc == EOS_OK);
    CHECK(hdr.hdr_size == EOS_FIXTURE_GOOD_HDR_SIZE);
    CHECK(hdr.image_size == EOS_FIXTURE_PAYLOAD_LEN);
    printf("  current layout [hdr][payload][tlv]: verify_integrity -> %d %s\n",
           rc, rc == EOS_OK ? "[PASS]" : "[FAIL]");

    /* Pre-fix output: [header][TLV][payload], so the bootloader hashes the
     * TLV block and the first bytes of the payload. It must refuse this. */
    memset(&hdr, 0, sizeof hdr);
    rc = stage_and_verify(eos_fixture_image_old, EOS_FIXTURE_OLD_LEN, &hdr);
    CHECK(rc != EOS_OK);
    printf("  old layout     [hdr][tlv][payload]: verify_integrity -> %d %s\n",
           rc, rc != EOS_OK ? "[PASS]" : "[FAIL]");

    /* hdr_size is 156 in both -- that is the point of the fix, and it is why
     * the two fixtures have to be distinguished by their bytes rather than by
     * a header field. Same payload, same key, different placement. */
    CHECK(EOS_FIXTURE_GOOD_HDR_SIZE == 156);
    CHECK(EOS_FIXTURE_OLD_HDR_SIZE == 156);
    CHECK(EOS_FIXTURE_GOOD_LEN == EOS_FIXTURE_OLD_LEN);
    CHECK(memcmp(eos_fixture_image_good, eos_fixture_image_old,
                 EOS_FIXTURE_GOOD_LEN) != 0);

    /* The finding this test was extended for: core/rollback.c computes the
     * TLV address as image_addr + hdr_size + image_size, i.e. it assumes the
     * area follows the payload. Under the old layout that landed past the end
     * of the image and the anti-rollback TLV could never be found -- so the
     * counter this repository gates downgrades on was unreachable for every
     * image the tool produced. verify_integrity alone does not exercise that
     * path; this does. */
    memset(&hdr, 0, sizeof hdr);
    rc = stage_and_verify(eos_fixture_image_good, EOS_FIXTURE_GOOD_LEN, &hdr);
    CHECK(rc == EOS_OK);
    {
        uint32_t tlv_addr = IMAGE_ADDR + hdr.hdr_size + hdr.image_size;
        uint16_t magic = 0;
        CHECK(eos_hal_flash_read(tlv_addr, &magic, sizeof magic) == EOS_OK);
        printf("  TLV magic at addr + hdr_size + image_size: 0x%04x %s\n",
               magic, magic == 0x6907 ? "[PASS]" : "[FAIL]");
        CHECK(magic == 0x6907);
        CHECK(hdr.tlv_len == EOS_FIXTURE_TLV_LEN);
    }

    if (failures) {
        printf("\n[FAIL] %d check(s) failed\n", failures);
        return 1;
    }
    printf("\n[PASS] the bootloader accepts current tool output and refuses the old layout\n");
    return 0;
}
