// SPDX-License-Identifier: MIT
// Copyright (c) 2026 EoS Project
// ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023

/**
 * @file test_fw_transport.c
 * @brief Unit tests for the UART firmware-update transports
 *
 * Drives the raw and YMODEM receive paths through the public ops tables
 * using a scripted UART and a simulated flash backend, so protocol
 * handling can be exercised on the host with no board port.
 */

#include "eos_fw_transport.h"
#include "eos_fw_update.h"
#include "eos_image.h"
#include "eos_image_tlv.h"
#include "eos_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- Simulated Flash ---- */

#define SIM_FLASH_SIZE   (128 * 1024)
static uint8_t sim_flash[SIM_FLASH_SIZE];

#define SIM_SLOT_A_ADDR  0x00000
#define SIM_SLOT_A_SIZE  0x08000
#define SIM_SLOT_B_ADDR  0x10000
#define SIM_SLOT_B_SIZE  0x08000

/* Written so the sum cannot wrap: a simulator that fails open would turn a
 * bounds bug in the code under test into a corrupted heap instead of a
 * legible test failure. */
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

/* ---- Scripted UART ---- */

#define RX_CAPACITY  8192
#define TX_CAPACITY  1024

static uint8_t rx_script[RX_CAPACITY];
static size_t  rx_len;
static size_t  rx_pos;

static uint8_t tx_capture[TX_CAPACITY];
static size_t  tx_len;

static int sim_uart_init(uint32_t baud) { (void)baud; return EOS_OK; }

static int sim_uart_send(const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    for (size_t i = 0; i < len && tx_len < TX_CAPACITY; i++) {
        tx_capture[tx_len++] = p[i];
    }
    return EOS_OK;
}

/* Returns TIMEOUT once the script is exhausted, which is how a real
 * transfer ends when the sender stops talking. */
static int sim_uart_recv(void *buf, size_t len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (rx_pos + len > rx_len) return EOS_ERR_TIMEOUT;
    memcpy(buf, &rx_script[rx_pos], len);
    rx_pos += len;
    return EOS_OK;
}

static uint32_t sim_tick_val = 0;
static uint32_t sim_get_tick(void) { return sim_tick_val++; }
static void sim_noop(void) {}
static void sim_noop_u32(uint32_t x) { (void)x; }
static void sim_jump(uint32_t addr) { (void)addr; }
static eos_reset_reason_t sim_reset_reason(void) { return EOS_RESET_POWER_ON; }
static bool sim_recovery_pin(void) { return false; }
static void sim_system_reset(void) {}

static const eos_board_ops_t sim_ops = {
    .flash_base          = 0,
    .flash_size          = SIM_FLASH_SIZE,
    .slot_a_addr         = SIM_SLOT_A_ADDR,
    .slot_a_size         = SIM_SLOT_A_SIZE,
    .slot_b_addr         = SIM_SLOT_B_ADDR,
    .slot_b_size         = SIM_SLOT_B_SIZE,
    .recovery_addr       = 0,
    .recovery_size       = 0,
    .bootctl_addr        = 0x1000,
    .bootctl_backup_addr = 0x2000,
    .log_addr            = 0x3000,
    .app_vector_offset   = 0,

    .flash_read          = sim_flash_read,
    .flash_write         = sim_flash_write,
    .flash_erase         = sim_flash_erase,

    .watchdog_init       = sim_noop_u32,
    .watchdog_feed       = sim_noop,
    .get_reset_reason    = sim_reset_reason,
    .system_reset        = sim_system_reset,
    .recovery_pin_asserted = sim_recovery_pin,
    .jump                = sim_jump,
    .uart_init           = sim_uart_init,
    .uart_send           = sim_uart_send,
    .uart_recv           = sim_uart_recv,
    .get_tick_ms         = sim_get_tick,
    .disable_interrupts  = sim_noop,
    .enable_interrupts   = sim_noop,
    .deinit_peripherals  = sim_noop,
};

static void setup(void)
{
    memset(sim_flash, 0xFF, sizeof(sim_flash));
    memset(rx_script, 0, sizeof(rx_script));
    memset(tx_capture, 0, sizeof(tx_capture));
    rx_len = 0;
    rx_pos = 0;
    tx_len = 0;
    sim_tick_val = 0;
    eos_hal_init(&sim_ops);
}

/* ---- Test harness (matches the other suites in tests/unit) ---- */

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
        setup(); \
        printf("  %-56s ", #name); \
        tests_run++; \
        name(); \
        tests_passed++; \
        printf("[OK]\n"); \
    } \
    static void name(void)

/* ---- Stream-building helpers ---- */

#define XM_SOH  0x01
#define XM_STX  0x02
#define XM_EOT  0x04
#define XM_ACK  0x06
#define XM_NAK  0x15
#define XM_CAN  0x18
#define BLOCK   128
#define BLOCK_L 1024

static void rx_push(const uint8_t *data, size_t n)
{
    ASSERT(rx_len + n <= RX_CAPACITY);
    memcpy(&rx_script[rx_len], data, n);
    rx_len += n;
}

static void rx_push_byte(uint8_t b) { rx_push(&b, 1); }

static uint16_t crc16_xmodem(const uint8_t *data, size_t len)
{
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else              crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* Append one YMODEM/XMODEM block with a correct CRC. `marker` selects the
 * framing: SOH for 128-byte blocks, STX for 1024-byte blocks. */
static void push_block_sized(uint8_t marker, uint8_t blk,
                             const uint8_t *payload, size_t n, size_t block_size)
{
    uint8_t body[BLOCK_L];
    memset(body, 0, block_size);
    if (n > block_size) n = block_size;
    if (payload) memcpy(body, payload, n);

    rx_push_byte(marker);
    rx_push_byte(blk);
    rx_push_byte((uint8_t)~blk);
    rx_push(body, block_size);

    uint16_t crc = crc16_xmodem(body, block_size);
    rx_push_byte((uint8_t)(crc >> 8));
    rx_push_byte((uint8_t)(crc & 0xFF));
}

/* Append one 128-byte SOH-framed block. */
static void push_block(uint8_t blk, const uint8_t *payload, size_t n)
{
    push_block_sized(XM_SOH, blk, payload, n, BLOCK);
}

/* Append a block whose complement byte is deliberately wrong. */
static void push_block_bad_complement(uint8_t blk)
{
    uint8_t body[BLOCK];
    memset(body, 0, sizeof(body));

    rx_push_byte(XM_SOH);
    rx_push_byte(blk);
    rx_push_byte((uint8_t)(~blk ^ 0xFF)); /* wrong */
    rx_push(body, BLOCK);

    uint16_t crc = crc16_xmodem(body, BLOCK);
    rx_push_byte((uint8_t)(crc >> 8));
    rx_push_byte((uint8_t)(crc & 0xFF));
}

/* YMODEM block 0: "name\0<decimal size>" */
static void push_header_block(const char *name, const char *size_str)
{
    uint8_t body[BLOCK];
    memset(body, 0, sizeof(body));
    size_t p = 0;
    size_t n = strlen(name);
    memcpy(body, name, n);
    p = n + 1;                       /* keep the NUL */
    if (size_str) memcpy(&body[p], size_str, strlen(size_str));
    push_block(0, body, BLOCK);
}

/* ---- Image construction ---- */

#define PAYLOAD_LEN  (3 * BLOCK - (int)sizeof(eos_image_header_t))

static uint8_t image_buf[3 * BLOCK];
static size_t  image_len;

/* Build header+payload occupying exactly three 128-byte blocks. */
static void build_image(void)
{
    eos_image_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic       = EOS_IMG_MAGIC;
    hdr.hdr_version = EOS_IMAGE_HDR_VERSION;
    hdr.hdr_size    = (uint16_t)sizeof(eos_image_header_t);
    hdr.image_size  = (uint32_t)PAYLOAD_LEN;
    hdr.load_addr   = SIM_SLOT_B_ADDR;
    hdr.entry_addr  = SIM_SLOT_B_ADDR;
    hdr.flags       = 0;   /* CRC32 integrity path */

    memset(image_buf, 0, sizeof(image_buf));
    memcpy(image_buf, &hdr, sizeof(hdr));
    for (int i = 0; i < PAYLOAD_LEN; i++) {
        image_buf[sizeof(hdr) + i] = (uint8_t)(0xA0 + (i & 0x0F));
    }
    image_len = sizeof(hdr) + (size_t)PAYLOAD_LEN;
}

static void push_image_block(int index)
{
    push_block((uint8_t)(index + 1), &image_buf[(size_t)index * BLOCK], BLOCK);
}

/* ---- Update container carrying an authenticated TLV area ----
 *
 * 156-byte header + 256-byte payload + 12-byte TLV area = 424 bytes, which is
 * three full 128-byte blocks plus 40. The final block therefore carries the
 * end of the payload, the payload-to-TLV transition, the whole TLV area and
 * 88 bytes of block padding -- every boundary the receiver has to get right.
 */

#define CONT_PAYLOAD_LEN  256u
#define CONT_TLV_LEN      (uint16_t)(sizeof(eos_tlv_info_t) + \
                                     sizeof(eos_tlv_entry_hdr_t) + \
                                     sizeof(uint32_t))
#define CONT_LEN          (sizeof(eos_image_header_t) + CONT_PAYLOAD_LEN + \
                           CONT_TLV_LEN)
#define CONT_BLOCKS       4

static uint8_t container[CONT_LEN];

/* Mirrors update_crc() in core/fw_update.c so finalize sees a matching CRC32
 * on the flags = 0 integrity path. */
static uint32_t crc32_payload(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320u;
            else         crc >>= 1;
        }
    }
    return ~crc;
}

static void build_container(void)
{
    eos_image_header_t hdr;
    uint8_t *payload = &container[sizeof(hdr)];
    uint8_t *tlv = &container[sizeof(hdr) + CONT_PAYLOAD_LEN];
    uint8_t digest[EOS_SHA256_DIGEST_SIZE];
    uint32_t sec_ver = 5;
    size_t i;

    memset(container, 0, sizeof(container));
    for (i = 0; i < CONT_PAYLOAD_LEN; i++)
        payload[i] = (uint8_t)(0x5A + (i & 0x1F));

    /* [tlv_info][entry_hdr][uint32 counter], the shape the update and
     * rollback suites build. */
    eos_tlv_info_t info = { EOS_TLV_INFO_MAGIC, CONT_TLV_LEN };
    eos_tlv_entry_hdr_t ent = { EOS_TLV_MIN_SEC_VER, sizeof(uint32_t) };
    memcpy(tlv, &info, sizeof(info));
    memcpy(tlv + sizeof(info), &ent, sizeof(ent));
    memcpy(tlv + sizeof(info) + sizeof(ent), &sec_ver, sizeof(sec_ver));

    memset(&hdr, 0, sizeof(hdr));
    hdr.magic       = EOS_IMG_MAGIC;
    hdr.hdr_version = EOS_IMAGE_HDR_VERSION;
    hdr.hdr_size    = (uint16_t)sizeof(hdr);
    hdr.image_size  = (uint32_t)CONT_PAYLOAD_LEN;
    hdr.load_addr   = SIM_SLOT_B_ADDR;
    hdr.entry_addr  = SIM_SLOT_B_ADDR;
    hdr.flags       = 0;              /* CRC32 integrity path */
    hdr.sig_type    = EOS_SIG_NONE;

    uint32_t crc = crc32_payload(payload, CONT_PAYLOAD_LEN);
    memcpy(hdr.hash, &crc, sizeof(crc));

    /* The TLV area is authenticated by the header hash, so rollback will
     * accept the counter it carries. */
    eos_sha256(tlv, CONT_TLV_LEN, digest);
    hdr.tlv_len = CONT_TLV_LEN;
    memcpy(hdr.tlv_hash, digest, EOS_IMG_TLV_HASH_LEN);

    memcpy(container, &hdr, sizeof(hdr));
}

/* The container as CONT_BLOCKS fixed 128-byte blocks, the last one padded. */
static void push_container_blocks(void)
{
    int i;

    for (i = 0; i < CONT_BLOCKS; i++) {
        size_t off = (size_t)i * BLOCK;
        size_t n = CONT_LEN - off;
        if (n > BLOCK) n = BLOCK;
        push_block((uint8_t)(i + 1), &container[off], n);
    }
}

static int run_ymodem_ctx(eos_fw_update_ctx_t *ctx)
{
    const eos_fw_transport_ops_t *ops = eos_fw_transport_uart_ymodem();
    eos_fw_transport_t tp;
    memset(&tp, 0, sizeof(tp));
    tp.ops = ops;
    tp.baudrate = 115200;
    tp.timeout_ms = 10;

    ASSERT(eos_fw_update_begin(ctx, EOS_SLOT_B) == EOS_OK);
    return ops->receive(&tp, ctx);
}

static int run_ymodem(void)
{
    eos_fw_update_ctx_t ctx;
    return run_ymodem_ctx(&ctx);
}

static int flash_matches_image(void)
{
    return memcmp(&sim_flash[SIM_SLOT_B_ADDR], image_buf, image_len) == 0;
}

static int slot_b_is_unwritten(void)
{
    for (size_t i = 0; i < image_len; i++)
        if (sim_flash[SIM_SLOT_B_ADDR + i] != 0xFF) return 0;
    return 1;
}

static int tx_contains(uint8_t b)
{
    for (size_t i = 0; i < tx_len; i++) if (tx_capture[i] == b) return 1;
    return 0;
}

/* ================================================================
 * YMODEM
 * ================================================================ */

/* Baseline: a well-formed transfer lands in flash byte-for-byte. */
TEST(test_ymodem_valid_transfer_is_written)
{
    build_image();
    push_header_block("fw.bin", "384");
    push_image_block(0);
    push_image_block(1);
    push_image_block(2);
    rx_push_byte(XM_EOT);
    rx_push_byte(XM_EOT);

    ASSERT(run_ymodem() == EOS_OK);
    ASSERT(flash_matches_image());
}

/*
 * Regression: the block number was read but never compared, so a block the
 * sender retransmits after a lost ACK was written to flash a second time,
 * shifting every following byte and corrupting the image.
 */
TEST(test_ymodem_duplicate_block_is_not_written_twice)
{
    build_image();
    push_header_block("fw.bin", "384");
    push_image_block(0);
    push_image_block(0);      /* retransmission of block 1 */
    push_image_block(1);
    push_image_block(2);
    rx_push_byte(XM_EOT);
    rx_push_byte(XM_EOT);

    ASSERT(run_ymodem() == EOS_OK);
    ASSERT(flash_matches_image());
}

/* An out-of-sequence block must be rejected, not written. */
TEST(test_ymodem_out_of_sequence_block_is_nakd)
{
    build_image();
    push_header_block("fw.bin", "384");
    push_image_block(0);
    push_block(9, &image_buf[BLOCK], BLOCK);   /* wrong sequence number */
    push_image_block(1);
    push_image_block(2);
    rx_push_byte(XM_EOT);
    rx_push_byte(XM_EOT);

    ASSERT(run_ymodem() == EOS_OK);
    ASSERT(tx_contains(XM_NAK));
    ASSERT(flash_matches_image());
}

/* A block whose complement byte does not match must be NAK'd. */
TEST(test_ymodem_bad_block_complement_is_nakd)
{
    build_image();
    push_header_block("fw.bin", "384");
    push_block_bad_complement(1);
    push_image_block(0);
    push_image_block(1);
    push_image_block(2);
    rx_push_byte(XM_EOT);
    rx_push_byte(XM_EOT);

    ASSERT(run_ymodem() == EOS_OK);
    ASSERT(tx_contains(XM_NAK));
    ASSERT(flash_matches_image());
}

/*
 * Block 0 is attacker-supplied and is not guaranteed to contain a NUL.
 * The filename scan is bounded by the block length; with no NUL there is no
 * size field behind it either, and a length the receiver cannot read is a
 * length it cannot use to tell image bytes from block padding.
 */
TEST(test_ymodem_header_without_nul_is_bounded)
{
    uint8_t body[BLOCK];
    memset(body, 'A', sizeof(body));   /* no NUL anywhere in the block */

    build_image();
    push_block(0, body, BLOCK);
    push_image_block(0);
    push_image_block(1);
    push_image_block(2);
    rx_push_byte(XM_EOT);
    rx_push_byte(XM_EOT);

    ASSERT(run_ymodem() == EOS_ERR_INVALID);
    ASSERT(slot_b_is_unwritten());
}

/*
 * The 1024-byte STX framing is where the original strlen() over-read actually
 * left the buffer: ymodem_receive()'s block[] is YMODEM_BLOCK_SIZE + 2 = 1026
 * bytes and a full STX block fills every one of them. Fill byte 0x21 is chosen
 * deliberately - its CRC over 1024 bytes is 0xE940, so neither CRC byte is NUL
 * either and the old unbounded scan had nothing to stop it before running off
 * the end. Under Valgrind this case reports an invalid read on unfixed code.
 */
TEST(test_ymodem_stx_header_without_nul_is_bounded)
{
    uint8_t body[BLOCK_L];
    memset(body, 0x21, sizeof(body));

    build_image();
    push_block_sized(XM_STX, 0, body, sizeof(body), BLOCK_L);
    push_image_block(0);
    push_image_block(1);
    push_image_block(2);
    rx_push_byte(XM_EOT);
    rx_push_byte(XM_EOT);

    ASSERT(run_ymodem() == EOS_ERR_INVALID);
    ASSERT(slot_b_is_unwritten());
}

/* Sequencing and duplicate suppression must apply on the STX path too. */
TEST(test_ymodem_stx_duplicate_block_is_not_written_twice)
{
    build_image();
    push_header_block("fw.bin", "384");
    push_block_sized(XM_STX, 1, image_buf, image_len, BLOCK_L);
    push_block_sized(XM_STX, 1, image_buf, image_len, BLOCK_L);  /* retransmit */
    rx_push_byte(XM_EOT);
    rx_push_byte(XM_EOT);

    ASSERT(run_ymodem() == EOS_OK);
    ASSERT(flash_matches_image());
}

/* A run of digits longer than 32 bits must not wrap into a bogus size. */
TEST(test_ymodem_header_size_overflow_is_rejected)
{
    build_image();
    push_header_block("fw.bin", "99999999999999999999");
    push_image_block(0);
    push_image_block(1);
    push_image_block(2);
    rx_push_byte(XM_EOT);
    rx_push_byte(XM_EOT);

    /* Size is discarded as unusable, and an unusable size is a protocol
     * error rather than a transfer of unknown length. */
    ASSERT(run_ymodem() == EOS_ERR_INVALID);
    ASSERT(slot_b_is_unwritten());
}

/* The first block must be block 0, not an arbitrary data block. */
TEST(test_ymodem_first_block_must_be_zero)
{
    build_image();
    push_image_block(0);        /* block 1 arriving first */
    rx_push_byte(XM_EOT);

    (void)run_ymodem();
    ASSERT(tx_contains(XM_NAK));
}

/*
 * The declared file size is YMODEM framing, and a sender is free to round it
 * up to its own file or block padding. The container is what the image itself
 * declares, and that is the authority on how many bytes are image, so the
 * surplus must be dropped rather than pushed into eos_fw_update_write().
 * Dropping it is correct framing, not a protocol error, so no CAN is sent.
 */
TEST(test_ymodem_declared_size_larger_than_container_is_clamped)
{
    eos_fw_update_ctx_t ctx;

    build_container();
    push_header_block("fw.bin", "512");   /* 424 bytes of image, padded to 512 */
    push_container_blocks();
    rx_push_byte(XM_EOT);
    rx_push_byte(XM_EOT);

    ASSERT(run_ymodem_ctx(&ctx) == EOS_OK);
    ASSERT(memcmp(&sim_flash[SIM_SLOT_B_ADDR], container, CONT_LEN) == 0);
    ASSERT(sim_flash[SIM_SLOT_B_ADDR + CONT_LEN] == 0xFF);
    ASSERT(eos_fw_update_get_state(&ctx) == EOS_FW_STATE_VERIFY);
    ASSERT(eos_fw_update_bytes_wanted(&ctx) == 0);
    ASSERT(ctx.tlv_written == CONT_TLV_LEN);
    ASSERT(!tx_contains(XM_CAN));
}

/*
 * Clamping the surplus inside the final block is framing. An entire further
 * data block once the container is complete is not, and ACKing it would let
 * a sender push an unbounded amount of data that no length field accounts
 * for. The declared size leaves room for the extra block, so this exercises
 * the container check rather than the file-size bound.
 */
TEST(test_ymodem_data_block_after_container_is_rejected)
{
    eos_fw_update_ctx_t ctx;
    uint8_t extra[BLOCK];

    build_container();
    push_header_block("fw.bin", "640");   /* 424 bytes of image, 5 blocks declared */
    push_container_blocks();

    memset(extra, 0x5C, sizeof(extra));
    push_block((uint8_t)(CONT_BLOCKS + 1), extra, sizeof(extra));
    rx_push_byte(XM_EOT);
    rx_push_byte(XM_EOT);

    ASSERT(run_ymodem_ctx(&ctx) == EOS_ERR_INVALID);
    ASSERT(tx_contains(XM_CAN));

    /* The container that did arrive is intact, and the extra block reached
     * neither flash nor the update context. */
    ASSERT(memcmp(&sim_flash[SIM_SLOT_B_ADDR], container, CONT_LEN) == 0);
    ASSERT(sim_flash[SIM_SLOT_B_ADDR + CONT_LEN] == 0xFF);
    ASSERT(ctx.tlv_written == CONT_TLV_LEN);

    /* eos_fw_transport_update() aborts the context when receive fails, so a
     * rejected transfer cannot then be finalized as a good image. */
    eos_fw_update_abort(&ctx);
    ASSERT(eos_fw_update_get_state(&ctx) == EOS_FW_STATE_IDLE);
    ASSERT(eos_fw_update_finalize(&ctx, EOS_UPGRADE_TEST) == EOS_ERR_INVALID);
}

/*
 * EOT ends the transfer, but it does not make a short transfer complete. The
 * handshake still runs -- no CAN -- and the truncated result is reported as
 * an error rather than as a received image.
 */
TEST(test_ymodem_eot_before_container_is_complete_is_rejected)
{
    eos_fw_update_ctx_t ctx;
    int i;

    build_container();
    push_header_block("fw.bin", "512");
    for (i = 0; i < CONT_BLOCKS - 1; i++)
        push_block((uint8_t)(i + 1), &container[(size_t)i * BLOCK], BLOCK);
    rx_push_byte(XM_EOT);
    rx_push_byte(XM_EOT);

    ASSERT(run_ymodem_ctx(&ctx) == EOS_ERR_INVALID);
    ASSERT(!tx_contains(XM_CAN));
    ASSERT(eos_fw_update_bytes_wanted(&ctx) != 0);
    ASSERT(eos_fw_update_get_state(&ctx) != EOS_FW_STATE_VERIFY);
    ASSERT(eos_fw_update_finalize(&ctx, EOS_UPGRADE_TEST) == EOS_ERR_INVALID);
}

/* ================================================================
 * XMODEM
 * ================================================================ */

static int run_xmodem(eos_fw_update_ctx_t *ctx)
{
    const eos_fw_transport_ops_t *ops = eos_fw_transport_uart_xmodem();
    eos_fw_transport_t tp;
    memset(&tp, 0, sizeof(tp));
    tp.ops = ops;
    tp.baudrate = 115200;
    tp.timeout_ms = 10;

    ASSERT(eos_fw_update_begin(ctx, EOS_SLOT_B) == EOS_OK);
    return ops->receive(&tp, ctx);
}

/*
 * Regression: the strict post-container check rejected the padding on the
 * last block, so any image that did not happen to be a multiple of 128
 * failed to install over XMODEM. The final block also carries the
 * payload-to-TLV transition and the whole TLV area.
 */
TEST(test_xmodem_padded_final_block_completes_and_finalizes)
{
    eos_fw_update_ctx_t ctx;

    build_container();
    ASSERT(CONT_LEN == 424);
    ASSERT(CONT_LEN % BLOCK != 0);
    ASSERT(CONT_BLOCKS * BLOCK - CONT_LEN == 88);

    push_container_blocks();
    rx_push_byte(XM_EOT);

    ASSERT(run_xmodem(&ctx) == EOS_OK);
    ASSERT(memcmp(&sim_flash[SIM_SLOT_B_ADDR], container, CONT_LEN) == 0);
    ASSERT(eos_fw_update_get_state(&ctx) == EOS_FW_STATE_VERIFY);
    ASSERT(eos_fw_update_bytes_wanted(&ctx) == 0);
    ASSERT(ctx.tlv_written == CONT_TLV_LEN);
    ASSERT(eos_fw_update_finalize(&ctx, EOS_UPGRADE_TEST) == EOS_OK);
}

/*
 * Padding in the remainder of the final block is framing the protocol
 * guarantees. An entire further data block after the container is complete is
 * not: XMODEM carries no length, so accepting those blocks would accept an
 * unbounded amount of data nothing accounts for.
 */
TEST(test_xmodem_data_block_after_container_is_rejected)
{
    eos_fw_update_ctx_t ctx;
    uint8_t extra[BLOCK];

    build_container();
    push_container_blocks();

    memset(extra, 0x5C, sizeof(extra));
    push_block((uint8_t)(CONT_BLOCKS + 1), extra, sizeof(extra));
    rx_push_byte(XM_EOT);

    ASSERT(run_xmodem(&ctx) == EOS_ERR_INVALID);
    ASSERT(tx_contains(XM_CAN));

    /* The container that did arrive is intact, and the extra block reached
     * neither flash nor the update context. */
    ASSERT(memcmp(&sim_flash[SIM_SLOT_B_ADDR], container, CONT_LEN) == 0);
    ASSERT(sim_flash[SIM_SLOT_B_ADDR + CONT_LEN] == 0xFF);

    /* eos_fw_transport_update() aborts the context when receive fails, so a
     * rejected transfer cannot then be finalized as a good image. */
    eos_fw_update_abort(&ctx);
    ASSERT(eos_fw_update_get_state(&ctx) == EOS_FW_STATE_IDLE);
    ASSERT(eos_fw_update_finalize(&ctx, EOS_UPGRADE_TEST) == EOS_ERR_INVALID);
}

/*
 * The XMODEM counterpart: the EOT handshake completes, but three of the four
 * blocks leave the payload and TLV area short, so the transfer is an error
 * rather than a received image.
 */
TEST(test_xmodem_eot_before_container_is_complete_is_rejected)
{
    eos_fw_update_ctx_t ctx;
    int i;

    build_container();
    for (i = 0; i < CONT_BLOCKS - 1; i++)
        push_block((uint8_t)(i + 1), &container[(size_t)i * BLOCK], BLOCK);
    rx_push_byte(XM_EOT);

    ASSERT(run_xmodem(&ctx) == EOS_ERR_INVALID);
    ASSERT(!tx_contains(XM_CAN));
    ASSERT(eos_fw_update_bytes_wanted(&ctx) != 0);
    ASSERT(eos_fw_update_get_state(&ctx) != EOS_FW_STATE_VERIFY);
    ASSERT(eos_fw_update_finalize(&ctx, EOS_UPGRADE_TEST) == EOS_ERR_INVALID);
}

/* ================================================================
 * Raw length-prefixed transport
 * ================================================================ */

static int run_raw_ctx(eos_fw_update_ctx_t *ctx)
{
    const eos_fw_transport_ops_t *ops = eos_fw_transport_uart_raw();
    eos_fw_transport_t tp;
    memset(&tp, 0, sizeof(tp));
    tp.ops = ops;
    tp.baudrate = 115200;
    tp.timeout_ms = 10;

    ASSERT(eos_fw_update_begin(ctx, EOS_SLOT_B) == EOS_OK);
    return ops->receive(&tp, ctx);
}

static int run_raw(void)
{
    eos_fw_update_ctx_t ctx;
    return run_raw_ctx(&ctx);
}

static void push_le32(uint32_t v)
{
    rx_push_byte((uint8_t)(v & 0xFF));
    rx_push_byte((uint8_t)((v >> 8) & 0xFF));
    rx_push_byte((uint8_t)((v >> 16) & 0xFF));
    rx_push_byte((uint8_t)((v >> 24) & 0xFF));
}

TEST(test_raw_valid_transfer_is_written)
{
    build_image();
    push_le32((uint32_t)image_len);
    rx_push(image_buf, image_len);

    ASSERT(run_raw() == EOS_OK);
    ASSERT(flash_matches_image());
    ASSERT(tx_contains(XM_ACK));
}

/*
 * The length prefix is the sender's framing, not the image's. A transfer
 * that delivers every declared byte but stops inside the container leaves
 * the TLV area outstanding, and must be NAK'd rather than acknowledged as a
 * received image.
 */
TEST(test_raw_transfer_shorter_than_container_is_rejected)
{
    eos_fw_update_ctx_t ctx;
    size_t short_len = CONT_LEN - CONT_TLV_LEN;   /* header + payload, no TLV */

    build_container();
    push_le32((uint32_t)short_len);
    rx_push(container, short_len);

    ASSERT(run_raw_ctx(&ctx) == EOS_ERR_INVALID);
    ASSERT(tx_contains(XM_NAK));
    ASSERT(eos_fw_update_bytes_wanted(&ctx) != 0);
    ASSERT(eos_fw_update_get_state(&ctx) != EOS_FW_STATE_VERIFY);
    ASSERT(eos_fw_update_finalize(&ctx, EOS_UPGRADE_TEST) == EOS_ERR_INVALID);
}

/*
 * Regression: the 4-byte length prefix is attacker-controlled and was used
 * unchecked, so a declared length of 0xFFFFFFFF drove a multi-gigabyte
 * receive loop. Anything larger than the slot is rejected up front.
 */
TEST(test_raw_oversized_length_is_rejected)
{
    build_image();
    push_le32(0xFFFFFFFFu);
    rx_push(image_buf, image_len);

    ASSERT(run_raw() == EOS_ERR_FULL);
    ASSERT(tx_contains(XM_NAK));
    ASSERT(rx_pos == 4);   /* stopped after the prefix; no payload consumed */
}

TEST(test_raw_zero_length_is_rejected)
{
    push_le32(0);

    ASSERT(run_raw() == EOS_ERR_FULL);
    ASSERT(tx_contains(XM_NAK));
}

/* ---- Main ---- */

int main(void)
{
    printf("=== eBootloader: Firmware Transport Unit Tests ===\n\n");

    run_test_ymodem_valid_transfer_is_written();
    run_test_ymodem_duplicate_block_is_not_written_twice();
    run_test_ymodem_out_of_sequence_block_is_nakd();
    run_test_ymodem_bad_block_complement_is_nakd();
    run_test_ymodem_header_without_nul_is_bounded();
    run_test_ymodem_stx_header_without_nul_is_bounded();
    run_test_ymodem_stx_duplicate_block_is_not_written_twice();
    run_test_ymodem_header_size_overflow_is_rejected();
    run_test_ymodem_first_block_must_be_zero();
    run_test_ymodem_declared_size_larger_than_container_is_clamped();
    run_test_ymodem_data_block_after_container_is_rejected();
    run_test_ymodem_eot_before_container_is_complete_is_rejected();
    run_test_xmodem_padded_final_block_completes_and_finalizes();
    run_test_xmodem_data_block_after_container_is_rejected();
    run_test_xmodem_eot_before_container_is_complete_is_rejected();
    run_test_raw_valid_transfer_is_written();
    run_test_raw_transfer_shorter_than_container_is_rejected();
    run_test_raw_oversized_length_is_rejected();
    run_test_raw_zero_length_is_rejected();

    tests_run = 19;
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
