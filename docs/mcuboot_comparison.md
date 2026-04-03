# mcuboot Comparison — eBootloader

> **Last updated:** 2026-04-03
>
> **Purpose:** Document the design decisions in eBootloader relative to mcuboot — what was adopted, what was intentionally not adopted, and why.

---

## 1. Overview

[mcuboot](https://www.mcuboot.com/) is the de facto open-source secure bootloader for microcontrollers, originally developed for Apache Mynewt and Zephyr. eBootloader draws inspiration from mcuboot's proven security patterns while making architectural decisions tailored to multi-architecture support, operational recovery, and staged boot.

---

## 2. Patterns Adopted from mcuboot

### 2.1 TLV Metadata

**mcuboot pattern:** Image metadata (hash, signature, key ID, dependencies) is stored as Type-Length-Value (TLV) entries appended after the image payload.

**eBootloader adoption:** The image header (`eos_image_header_t`) uses a fixed-layout header with reserved fields for future TLV extension. The `sign_image.py` tool supports TLV generation for key hash and dependency information.

| TLV Type | mcuboot | eBootloader |
|---|---|---|
| SHA-256 hash | `IMAGE_TLV_SHA256` (0x10) | `hash[32]` field in fixed header |
| Key hash | `IMAGE_TLV_KEYHASH` (0x01) | Planned — TLV extension after header |
| Ed25519 signature | `IMAGE_TLV_ED25519` (0x24) | `signature[64]` field in fixed header |
| Security counter | `IMAGE_TLV_SEC_CNT` (0x50) | `image_version` field + dedicated counter |

**Rationale:** A fixed-layout header with known offsets simplifies stage-0 parsing (minimal code) and enables constant-time field access. TLV is used for optional/extensible metadata only.

### 2.2 Hash-Then-Verify-Signature

**mcuboot pattern:** First compute the hash of the image payload, then verify the signature over the hash. This separates integrity from authenticity.

**eBootloader adoption:** Identical approach:

```
Step 1: eos_image_verify_integrity()
  → Compute SHA-256 over payload
  → Compare against header hash field
  → FAIL → reject image (integrity violation)

Step 2: eos_image_verify_signature()
  → Verify Ed25519 signature over header hash
  → FAIL → reject image (authenticity violation)
```

**Rationale:** This is a well-established pattern that enables:
- Early rejection of corrupted images without expensive signature verification
- Separate integrity and authenticity error reporting
- Compatibility with CRC-only mode (Phase 1) where signature verification is absent

### 2.3 Key Hash in TLV

**mcuboot pattern:** Instead of embedding the full public key in the image, mcuboot stores a SHA-256 hash of the public key in the TLV. The bootloader holds the actual public key and compares hashes to select which key to use for verification.

**eBootloader adoption:** Planned for Phase 2. The image will carry a key hash identifying which key slot was used for signing. The bootloader compares this hash against its embedded public key hashes to select the correct verification key.

**Benefit:** Enables efficient multi-key support — the bootloader can determine which of its key slots to use without attempting verification with each key.

### 2.4 Security Counter

**mcuboot pattern:** A monotonic security counter is stored in the image TLV (`IMAGE_TLV_SEC_CNT`). The bootloader compares it against a device-side counter and rejects images with a lower counter.

**eBootloader adoption:** Monotonic counter stored in OTP/eFuse or dedicated flash sector. See [Key Lifecycle — §6](key_lifecycle.md#6-key-revocation-via-monotonic-counter).

---

## 3. Patterns Intentionally Not Adopted

### 3.1 Swap-Based Upgrade

**mcuboot approach:** By default, mcuboot uses a swap-based upgrade strategy. When a new image is placed in the secondary slot, mcuboot swaps the primary and secondary slot contents sector-by-sector. This allows automatic revert if the new image fails to boot.

**eBootloader decision: Not adopted.**

**Reasons:**

| Factor | Swap-Based (mcuboot) | Direct-Write A/B (eBootloader) |
|---|---|---|
| Flash wear | 3× write amplification (read A, read B, write A, write B, write scratch) | 1× write (write to inactive slot) |
| Boot time | Slow first boot after upgrade (swap takes seconds) | Instant — no data movement at boot |
| Scratch area | Requires dedicated scratch partition | Not needed |
| Code complexity | Complex swap state machine with resume-on-interrupt | Simple: select slot, verify, jump |
| Anti-tearing | Swap status tracked in dedicated area | Only boot control block needs anti-tearing |
| Flash size | Requires scratch area (~1 sector minimum) | No overhead |
| Recovery | Automatic revert via swap-back | Rollback by switching `active_slot` pointer |

**eBootloader approach:** Direct-write with A/B slot selection. The inactive slot is written by the application. On reboot, stage-1 verifies the candidate image and either boots it (test mode) or falls back to the confirmed slot. No data is moved between slots at boot time.

### 3.2 Overwrite-Only Upgrade

**mcuboot approach:** An alternative mode where the secondary slot image is copied to the primary slot, destroying the previous primary image.

**eBootloader decision: Not adopted.**

**Reason:** Overwrite-only eliminates rollback capability. eBootloader always maintains two slot copies for resilience.

### 3.3 Direct-XIP (Execute in Place)

**mcuboot approach:** Both slots contain fully position-independent images. mcuboot selects the slot to execute from without copying.

**eBootloader decision: Partially adopted.** eBootloader's A/B slots use fixed load addresses, but images are linked for their specific slot address. The boot manager selects which slot to jump to — similar to direct-XIP but with fixed addressing rather than position-independent code.

---

## 4. Architectural Differences

### 4.1 HAL Abstraction

| Aspect | mcuboot | eBootloader |
|---|---|---|
| Flash interface | `flash_area` API (`flash_area_open`, `flash_area_read`, etc.) | `eos_board_ops_t` vtable with function pointers |
| Registration | Compile-time via `flash_map` configuration | Runtime via `eos_hal_init(&board_ops)` |
| Multi-board | Board selection via Kconfig/CMake | Board selection via `board_get_ops()` at runtime |
| Watchdog | Not managed by mcuboot | `eos_board_ops_t.watchdog_init/feed` |
| UART | Not part of mcuboot HAL | `eos_board_ops_t.uart_init/send/recv` for recovery |
| Reset control | Not part of mcuboot HAL | `eos_board_ops_t.system_reset`, `get_reset_reason` |
| Jump/handoff | Direct function call to app entry | `eos_board_ops_t.jump` with MSP setup via `eos_hal_set_msp()` |

**Rationale:** The vtable approach allows a single eBootloader binary to support multiple boards via runtime board detection (useful for SoC families with board variants). It also integrates recovery, watchdog, and UART into the same HAL — mcuboot delegates these to the underlying RTOS.

### 4.2 Multi-Architecture Support

| Architecture | mcuboot | eBootloader |
|---|---|---|
| ARM Cortex-M (M0, M3, M4, M7, M33) | ✅ Primary target | ✅ Supported |
| ARM Cortex-A (A53, A72) | ❌ | ✅ Supported |
| ARM Cortex-R (R5F) | ❌ | ✅ Supported |
| RISC-V (RV32, RV64) | ✅ (limited) | ✅ Supported |
| x86 / x86_64 | ❌ | ✅ Supported |
| Xtensa | ❌ | ✅ Supported |
| PowerPC | ❌ | ✅ Platform enum defined |
| SPARC, M68K, SH4, MIPS, etc. | ❌ | ✅ Platform enum defined |

eBootloader's `eos_platform_t` enum in `eos_hal.h` defines 24 platform targets. The `eos_hal_set_msp()` inline function already includes architecture-specific assembly for ARM Cortex-M, Cortex-A/R, AArch64, and RISC-V.

### 4.3 Boot Stages

| Aspect | mcuboot | eBootloader |
|---|---|---|
| Stage count | 2 (ROM → mcuboot → app) | 3 (ROM → stage-0 → stage-1 → app) |
| First-stage size | ~32–64 KB typical | 16 KB (stage-0) + 48 KB (stage-1) |
| Stage-0 purpose | N/A | Minimal init, recovery check, stage-1 verification |
| Recovery | External tooling | Built-in UART recovery with 9-command protocol |

**Rationale:** The two-stage split (stage-0 + stage-1) keeps the root-of-trust code surface minimal (16 KB). Stage-0 can be locked in flash and rarely updated, while stage-1 can be updated more frequently with new boot policies. mcuboot is a single monolithic bootloader.

### 4.4 Recovery Mechanism

| Aspect | mcuboot | eBootloader |
|---|---|---|
| Built-in recovery | ❌ No | ✅ UART command protocol (9 commands) |
| Recovery trigger | N/A | Hardware pin (`recovery_pin_asserted`) or boot control flag |
| Remote recovery | Depends on app/RTOS | UART: PING, INFO, ERASE, WRITE, VERIFY, BOOT, LOG, RESET, FACTORY |
| Brick recovery | External programmer (JTAG/SWD) | UART recovery before JTAG is needed |

---

## 5. Security Features Comparison

| Security Feature | mcuboot | eBootloader | Notes |
|---|---|---|---|
| **Image integrity** | SHA-256 in TLV | SHA-256 in header (Phase 2), CRC32 (Phase 1) | Same algorithm, different metadata format |
| **Signature algorithms** | RSA-2048/3072, ECDSA-P256, Ed25519 | Ed25519 (primary), ECDSA (planned) | eBootloader focuses on Ed25519 for size/speed |
| **Key storage** | Compiled-in public key hash | Compiled-in, OTP/eFuse, or secure element | eBootloader supports more storage options |
| **Multi-key support** | Up to N keys via key hash TLV | Dual-slot (primary + backup) | Sufficient for rotation; simpler implementation |
| **Anti-rollback** | Security counter in TLV | Monotonic counter (OTP/flash/SE) | Same concept, different storage |
| **Payload encryption** | AES-256-CTR, AES-256-CBC, ECIES-P256+AES | AES-256-GCM (Phase 3) | eBootloader uses authenticated encryption |
| **Swap protection** | Swap status tracking | Not applicable (no swap) | eBootloader avoids swap complexity |
| **Debug lock** | Not managed | Stage-0 applies debug lock in production | Integrated into boot chain |
| **Recovery auth** | Not applicable | Challenge-response (Phase 3) | eBootloader authenticates recovery sessions |
| **Boot logging** | Minimal | Dedicated boot log region (8 KB) | Full audit trail of boot decisions |
| **Hardware dependencies** | Requires RTOS flash driver | Bare-metal HAL vtable | eBootloader has no RTOS dependency |

---

## 6. When to Choose Which

| Scenario | Recommended | Reason |
|---|---|---|
| Zephyr/Mynewt project on Cortex-M | mcuboot | Native integration, community support |
| Multi-arch project (ARM + RISC-V + x86) | eBootloader | Broader platform support |
| Bare-metal with no RTOS | eBootloader | No RTOS dependency; self-contained HAL |
| Need built-in UART recovery | eBootloader | 9-command recovery protocol included |
| Need swap-based upgrade with scratch | mcuboot | Mature swap implementation |
| Need minimal flash overhead | eBootloader | No scratch partition; A/B direct-write |
| Need boot audit logging | eBootloader | Dedicated boot log region |
| Existing mcuboot deployment | mcuboot | Migration cost outweighs benefits |

---

## References

- [mcuboot Documentation](https://docs.mcuboot.com/)
- [Secure Boot Chain](secure_boot_chain.md)
- [Architecture](architecture.md)
- [Security Model](security.md)
- [Key Lifecycle](key_lifecycle.md)
