# Secure Boot Chain — eBootloader

> **Last updated:** 2026-04-03
>
> **Scope:** Complete chain of trust from ROM through application firmware in the eBootloader system.

---

## 1. Chain of Trust Overview

The eBootloader implements a staged chain of trust where each boot stage verifies the next before transferring execution. Every link in the chain must pass verification — a single failure triggers recovery or halt.

```
┌─────────────────────────────────────────────────────────────────────┐
│                        SILICON ROM                                  │
│                  (immutable root of trust)                          │
│                                                                     │
│  • Loads from fixed flash address (0x08000000)                      │
│  • Platform-dependent: may verify stage-0 hash via eFuse/OTP       │
│  • Sets initial stack pointer and jumps to Reset_Handler            │
└────────────────────────┬────────────────────────────────────────────┘
                         │ TB-1: ROM → Stage-0
                         │ Verified by: ROM hash (SoC-dependent)
                         ▼
┌─────────────────────────────────────────────────────────────────────┐
│                     STAGE-0 (eBootloader)                           │
│                       16 KB flash region                            │
│                                                                     │
│  • Copy .data, zero .bss                                            │
│  • ebldr_hw_init_minimal() — clocks, flash latency                 │
│  • board_get_ops() — register HAL vtable                            │
│  • ebldr_watchdog_init()                                            │
│  • eos_bootctl_load() — load boot state from flash                 │
│  • Check recovery trigger (pin or flag)                             │
│  • ★ VERIFY stage-1 image (hash + signature)                       │
│  • Jump to stage-1 entry point                                      │
└────────────────────────┬────────────────────────────────────────────┘
                         │ TB-2: Stage-0 → Stage-1
                         │ Verified by: SHA-256 + Ed25519 signature
                         ▼
┌─────────────────────────────────────────────────────────────────────┐
│                     STAGE-1 (E-Boot)                                │
│                      48 KB flash region                             │
│                                                                     │
│  • eos_bootctl_load() — reload boot state                           │
│  • eos_boot_log_init() — initialize boot log                       │
│  • Check recovery trigger (pin, flag, max attempts)                 │
│  • eboot_scan_images() — parse and verify both slots                │
│  • ★ VERIFY application image (hash + signature + version)          │
│  • eboot_select_slot() — boot policy decision                      │
│  • Anti-rollback check (security counter)                           │
│  • eboot_jump_to_app() — transfer to application                   │
└────────────────────────┬────────────────────────────────────────────┘
                         │ TB-3: Stage-1 → Application
                         │ Verified by: SHA-256 + Ed25519 + anti-rollback
                         ▼
┌─────────────────────────────────────────────────────────────────────┐
│                   APPLICATION FIRMWARE                               │
│                   448 KB × 2 slots (A/B)                            │
│                                                                     │
│  • Application entry point (vector table)                           │
│  • Self-test → eos_fw_confirm_running_image()                       │
│  • Runtime firmware services via eos_fwsvc API                      │
│  • OTA update: write to inactive slot + eos_fw_request_upgrade()    │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 2. Verification Steps at Each Stage

### 2.1 ROM → Stage-0 (TB-1)

| Step | Operation | API / Mechanism |
|---|---|---|
| 1 | ROM reads stage-0 vector table from `0x08000000` | SoC boot ROM (vendor) |
| 2 | ROM verifies stage-0 hash against OTP/eFuse value | SoC-specific (e.g., STM32 Secure Boot, nRF Secure Boot) |
| 3 | ROM sets MSP from vector table entry 0 | Hardware |
| 4 | ROM jumps to Reset_Handler (vector table entry 1) | Hardware |

**What is verified:**
- Stage-0 binary integrity (hash match)
- Stage-0 authenticity (if ROM supports signature verification)

**What is NOT verified by ROM:**
- Stage-1 or application images (responsibility of stage-0 and stage-1)
- Boot control block integrity (loaded later by stage-0)

### 2.2 Stage-0 → Stage-1 (TB-2)

| Step | Operation | API |
|---|---|---|
| 1 | Initialize minimal hardware | `ebldr_hw_init_minimal()` |
| 2 | Register board HAL | `board_get_ops()` → `eos_hal_init()` |
| 3 | Start watchdog | `ebldr_watchdog_init()` |
| 4 | Load boot control block | `eos_bootctl_load()` |
| 5 | Check recovery trigger | `ebldr_recovery_triggered()` |
| 6 | Parse stage-1 image header at `0x08004000` | `eos_image_parse_header()` |
| 7 | Verify stage-1 CRC32 (Phase 1) or SHA-256 hash (Phase 2) | `eos_image_verify_integrity()` |
| 8 | Verify stage-1 Ed25519 signature (Phase 2) | `eos_image_verify_signature()` |
| 9 | Check stage-1 version against minimum (Phase 2) | `eos_image_check_version()` |
| 10 | Disable interrupts, deinit peripherals | `eos_hal_disable_interrupts()` |
| 11 | Set MSP and jump to stage-1 | `eos_hal_set_msp()` → `eos_hal_jump()` |

**Verification flow (Phase 2):**

```c
int stage0_verify_stage1(void) {
    eos_image_header_t hdr;
    int rc;

    rc = eos_image_parse_header(STAGE1_ADDR, &hdr);
    if (rc != EOS_OK) return rc;

    rc = eos_image_verify_integrity(&hdr, STAGE1_ADDR + hdr.hdr_size);
    if (rc != EOS_OK) return rc;

    rc = eos_image_verify_signature(&hdr);
    if (rc != EOS_OK) return rc;

    rc = eos_image_check_version(hdr.image_version, min_stage1_version);
    if (rc != EOS_OK) return rc;

    return EOS_OK;
}
```

### 2.3 Stage-1 → Application (TB-3)

| Step | Operation | API |
|---|---|---|
| 1 | Reload boot control block | `eos_bootctl_load()` |
| 2 | Initialize boot log | `eos_boot_log_init()` |
| 3 | Check recovery conditions | `eboot_should_recover()` |
| 4 | Scan both image slots (A and B) | `eboot_scan_images()` |
| 5 | Parse image header for candidate slot | `eos_image_parse_header()` |
| 6 | Verify image integrity (CRC32 / SHA-256) | `eos_image_verify_integrity()` |
| 7 | Verify image signature (Ed25519) | `eos_image_verify_signature()` |
| 8 | Check image version against security counter | `eos_image_check_version()` |
| 9 | Apply boot policy (test boot, rollback, fallback) | `eboot_select_slot()` |
| 10 | Increment boot attempts | `eos_bootctl_increment_attempts()` |
| 11 | Disable interrupts, deinit peripherals | `eos_hal_disable_interrupts()`, `eos_hal_deinit_peripherals()` |
| 12 | Jump to application vector table | `eboot_jump_to_app()` |

---

## 3. Failure Modes and Recovery Paths

### 3.1 Failure Mode Summary

| Stage | Failure | Detection | Recovery |
|---|---|---|---|
| ROM → Stage-0 | Stage-0 hash mismatch | ROM verification | Device-specific (JTAG/SWD reflash, ROM recovery mode) |
| ROM → Stage-0 | Stage-0 corrupted | ROM verification or crash | JTAG/SWD reflash |
| Stage-0 → Stage-1 | Stage-1 header invalid | `eos_image_parse_header()` returns error | Enter UART recovery mode |
| Stage-0 → Stage-1 | Stage-1 integrity check fails | `eos_image_verify_integrity()` returns `EOS_ERR_CRC` | Enter UART recovery mode |
| Stage-0 → Stage-1 | Stage-1 signature invalid | `eos_image_verify_signature()` returns `EOS_ERR_SIGNATURE` | Enter UART recovery mode |
| Stage-0 → Stage-1 | Stage-1 version too old | `eos_image_check_version()` returns `EOS_ERR_VERSION` | Enter UART recovery mode |
| Stage-1 → App | Active slot image invalid | Integrity/signature check fails | Try alternate slot (B→A or A→B) |
| Stage-1 → App | Both slots invalid | Both fail verification | Enter UART recovery mode |
| Stage-1 → App | Boot attempts exceeded | `boot_attempts >= max_attempts` | Rollback to `confirmed_slot` |
| Stage-1 → App | Application crashes on test boot | Watchdog reset; `boot_attempts` incremented | After `max_attempts`: rollback |
| Stage-1 → App | Application fails self-test | App calls `eos_fw_request_recovery()` | Recovery mode on next boot |
| Any stage | Boot control block corrupt | `eos_bootctl_validate()` returns false | Use backup copy; if both corrupt, init defaults |

### 3.2 Recovery Decision Tree

```
Stage-0 verification of stage-1:
  ├── PASS → jump to stage-1
  └── FAIL
      ├── Recovery pin asserted? → YES → UART recovery
      └── NO → attempt recovery anyway (no valid stage-1)

Stage-1 verification of application:
  ├── Active slot PASS
  │   ├── Test boot?
  │   │   ├── YES → boot with attempt tracking
  │   │   └── NO → boot normally
  │   └── Boot
  ├── Active slot FAIL
  │   ├── Alternate slot PASS → boot alternate
  │   └── Alternate slot FAIL → UART recovery
  └── max_attempts exceeded → rollback to confirmed_slot
```

### 3.3 Unrecoverable Conditions

| Condition | Cause | Resolution |
|---|---|---|
| ROM verification fails (if enabled) | Corrupted stage-0 in flash | JTAG/SWD reflash required |
| All OTP key slots revoked | Incorrect key revocation sequence | Device is permanently bricked — hardware replacement |
| Flash hardware failure | Physical damage, wear-out | Hardware replacement |
| Debug interface locked + no valid firmware | Debug lock prevents reflash | Contact manufacturer for unlock (if supported) |

---

## 4. Anti-Rollback Mechanism

### 4.1 Monotonic Security Counter

Each firmware image carries a `security_counter` field in its header. The device maintains a monotonic counter in persistent storage that can only increase.

```
┌─────────────┐        ┌─────────────────┐
│ Image Header│        │ Device Storage   │
│             │        │                  │
│ security_   │───────>│ security_version │
│ counter: N  │ compare│ (monotonic): M   │
│             │        │                  │
└─────────────┘        └─────────────────┘

  If N >= M → ACCEPT, update M to N
  If N <  M → REJECT (rollback attempt)
```

### 4.2 Counter Storage Options

| Method | Storage | Properties |
|---|---|---|
| OTP/eFuse bits | One-time programmable fuses | Irreversible; limited counter range (depends on fuse count) |
| Flash sector | Dedicated flash sector with anti-tearing | Reversible with physical access; unlimited range |
| Secure element | SE-managed counter | Irreversible; high counter range; tamper-resistant |

### 4.3 Version vs. Security Counter

| Field | Purpose | Comparison |
|---|---|---|
| `image_version` | Human-readable version (major.minor.patch) | Informational; logged in boot log |
| `security_counter` | Anti-rollback enforcement | Compared against device monotonic counter |

The `security_counter` is incremented independently of `image_version`. A minor patch may not increment the security counter, while a critical security fix always does.

---

## 5. Comparison with mcuboot's Approach

| Aspect | eBootloader | mcuboot |
|---|---|---|
| Boot stages | 3-stage (ROM → stage-0 → stage-1 → app) | 2-stage (ROM → mcuboot → app) |
| Upgrade strategy | Direct-write A/B slots | Swap-based (default), overwrite, or direct-XIP |
| Image verification | Hash-then-verify-signature (same as mcuboot) | Hash-then-verify-signature |
| Anti-rollback | Monotonic counter (OTP/flash/SE) | Security counter in image TLV |
| HAL abstraction | `eos_board_ops_t` vtable (function pointers) | `flash_area` API |
| Multi-arch | 24+ platform targets | Primarily ARM Cortex-M |
| Recovery | Dedicated UART recovery protocol | No built-in recovery; depends on external tools |
| Metadata format | Fixed-layout image header + optional TLV | TLV-based metadata after image |

See [mcuboot Comparison](mcuboot_comparison.md) for a detailed analysis.

---

## 6. Debug Interface State at Each Boot Stage

The debug interface (JTAG/SWD) state transitions through the boot chain to balance development needs with production security.

### 6.1 Debug State Matrix

| Boot Stage | Development Build | Production Build |
|---|---|---|
| ROM | Enabled (default) | Enabled (ROM cannot change) |
| Stage-0 entry | Enabled | Enabled (not yet configured) |
| Stage-0 after init | Enabled | **Locked** — debug lock applied via `board_early_init()` |
| Stage-1 | Enabled | Locked (inherited from stage-0) |
| Application | Enabled | Locked (inherited) |

### 6.2 Debug Lock Implementation

```c
void board_early_init(void) {
    /* ... clock and flash init ... */

#if defined(EOS_BUILD_PRODUCTION)
    /* Lock debug interface — platform-specific */
    #if defined(STM32F4)
        /* Set RDP Level 1 (or Level 2 for permanent lock) */
        HAL_FLASH_OB_Unlock();
        FLASH_OBProgramInitTypeDef ob = { .RDPLevel = OB_RDP_LEVEL_1 };
        HAL_FLASHEx_OBProgram(&ob);
        HAL_FLASH_OB_Lock();
    #elif defined(NRF52)
        NRF_APPROTECT->FORCEPROTECT = 1;
    #endif
#endif
}
```

### 6.3 Debug Lock Policy

| Policy | Description |
|---|---|
| **Development** | Debug always enabled. Well-known development signing key used. |
| **Staging** | Debug enabled via flag. Staging signing key used. |
| **Production** | Debug locked at stage-0. Production signing key. Unlock requires authenticated procedure (platform-dependent). |

---

## References

- [Architecture](architecture.md)
- [Key Lifecycle](key_lifecycle.md)
- [Threat Model](threat_model.md)
- [Security Model](security.md)
- [mcuboot Comparison](mcuboot_comparison.md)
- [Update Flow](update-flow.md)
