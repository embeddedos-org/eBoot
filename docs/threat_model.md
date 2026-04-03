# Threat Model — eBootloader

> **Methodology:** STRIDE (Spoofing, Tampering, Repudiation, Information Disclosure, Denial of Service, Elevation of Privilege)
>
> **Last updated:** 2026-04-03
>
> **Scope:** eBootloader staged boot system (stage-0, stage-1, recovery, flash storage, firmware update pipeline)

---

## 1. Assumptions

### 1.1 Physical Access Model

| Assumption | Description |
|---|---|
| **Field deployment** | Devices are deployed in physically accessible locations (edge gateways, industrial controllers, IoT endpoints). An attacker may gain physical access to the device enclosure. |
| **Debug ports** | JTAG/SWD debug headers may be populated on development boards but should be disabled or locked in production builds. |
| **UART access** | The recovery UART is accessible with enclosure removal. Production builds must authenticate recovery sessions. |
| **Chip decapping** | Invasive silicon attacks (decapping, FIB probing) are out of scope for the base threat model. Devices requiring this level of protection should use a discrete secure element. |

### 1.2 Deployment Context

| Parameter | Value |
|---|---|
| Target platforms | Cortex-M0/M3/M4/M7/M33, Cortex-A53/A72, Cortex-R5F, RISC-V RV32/RV64 |
| Connectivity | Devices may be network-connected (OTA updates via application firmware) |
| Lifetime | 5–15 year field deployment |
| Update frequency | Quarterly security patches, ad-hoc critical fixes |
| Regulatory context | ISO/IEC 20243 (supply chain), ISO/IEC/IEEE 15288:2023 (secure lifecycle) |

---

## 2. Trust Boundaries

```
┌──────────────────────────────────────────────────┐
│                   Silicon ROM                     │
│           (immutable, vendor-provided)            │
├─ ─ ─ ─ ─ ─ TB-1: ROM → Stage-0 ─ ─ ─ ─ ─ ─ ─ ─┤
│              Stage-0 (eBootloader)                │
│      16 KB — minimal HW init, recovery check      │
├─ ─ ─ ─ ─ ─ TB-2: Stage-0 → Stage-1 ─ ─ ─ ─ ─ ─┤
│              Stage-1 (E-Boot)                     │
│    48 KB — image scan, boot policy, boot log      │
├─ ─ ─ ─ ─ ─ TB-3: Stage-1 → Application ─ ─ ─ ─ ┤
│              Application Firmware                 │
│         448 KB × 2 slots (A/B)                    │
└──────────────────────────────────────────────────┘

External boundaries:
  TB-4: UART Recovery Interface (physical)
  TB-5: Flash Storage (internal/external NOR/NAND)
  TB-6: Debug Interface (JTAG/SWD)
  TB-7: Firmware Update Pipeline (OTA → application → slot write)
```

### Boundary Descriptions

| Boundary | From | To | Data Crossing | Protection |
|---|---|---|---|---|
| TB-1 | Silicon ROM | Stage-0 | Execution handoff, vector table | ROM validates stage-0 hash (SoC-dependent) |
| TB-2 | Stage-0 | Stage-1 | Execution handoff, HAL vtable | Stage-0 verifies stage-1 image header + hash/signature |
| TB-3 | Stage-1 | Application | Execution handoff, boot state | Stage-1 verifies application image header + hash/signature |
| TB-4 | External host | Stage-0/Stage-1 | UART packets (PING, ERASE, WRITE, BOOT, etc.) | Packet format validation; authentication planned (Phase 3) |
| TB-5 | Flash IC | CPU | Image payloads, boot control block, boot log | CRC32 (Phase 1), SHA-256 + signatures (Phase 2), encryption (Phase 3) |
| TB-6 | Debug probe | CPU | Memory read/write, halt, step | Debug lock bits (platform-dependent) |
| TB-7 | Cloud/server | Application → Flash | Firmware image bytes | Application-layer TLS; bootloader verifies image before activation |

---

## 3. Attack Surfaces

### 3.1 UART Recovery Protocol

- **Interface:** `eos_hal_uart_send()` / `eos_hal_uart_recv()` via `eos_board_ops_t` function pointers
- **Packet format:** `[cmd:1][slot:1][len:2][offset:4]` — 8-byte header
- **Commands exposed:** PING, INFO, ERASE, WRITE, VERIFY, BOOT, LOG, RESET, FACTORY
- **Attack vector:** Malformed packets, unauthorized firmware replacement, command injection

### 3.2 Flash Contents

- **Regions:** Slot A (448 KB), Slot B (448 KB), Recovery (16 KB), Boot control primary (8 KB), Boot control backup (8 KB), Boot log (8 KB), Device config (24 KB)
- **Attack vector:** Direct flash modification (physical), corrupted OTA writes, bit-flip attacks

### 3.3 JTAG/SWD Debug Interface

- **Attack vector:** Memory extraction, firmware dumping, runtime state manipulation, bypassing boot verification
- **Notes:** Debug lock mechanism is platform-dependent (e.g., STM32 RDP level, nRF APPROTECT)

### 3.4 Firmware Update Pipeline

- **Flow:** Cloud → Application (TLS download) → `eos_hal_flash_write()` to inactive slot → `eos_fw_request_upgrade()` → reboot → Stage-1 verification
- **Attack vector:** Man-in-the-middle on OTA channel, supply chain compromise, malicious update server

### 3.5 Boot Control Block

- **Structure:** `eos_bootctl_t` — 96 bytes with CRC32 protection
- **Fields at risk:** `active_slot`, `pending_slot`, `boot_attempts`, `max_attempts`, `flags`
- **Attack vector:** Corrupt control block to force boot to attacker-controlled slot, reset boot attempts to prevent rollback, set factory reset flag

---

## 4. Threat Matrix

### TB-1: ROM → Stage-0

| ID | STRIDE | Threat | Likelihood | Impact | Risk | Mitigation | Status |
|---|---|---|---|---|---|---|---|
| T-101 | Tampering | Attacker modifies stage-0 binary in flash | Low | Critical | High | ROM hash verification (SoC-dependent); debug lock | Planned |
| T-102 | Elevation of Privilege | Stage-0 contains vulnerability allowing arbitrary code execution | Low | Critical | High | Minimal code surface (16 KB); no dynamic memory; stack bounds | Mitigated |
| T-103 | Information Disclosure | Stage-0 leaks key material or secrets via UART during early init | Low | High | Medium | No secrets processed in stage-0; UART output limited to recovery mode | Mitigated |

### TB-2: Stage-0 → Stage-1

| ID | STRIDE | Threat | Likelihood | Impact | Risk | Mitigation | Status |
|---|---|---|---|---|---|---|---|
| T-201 | Tampering | Attacker replaces stage-1 binary with malicious code | Medium | Critical | High | SHA-256 hash verification of stage-1 image (Phase 2); Ed25519 signature verification (Phase 2) | Planned |
| T-202 | Spoofing | Attacker presents old stage-1 version to exploit known vulnerability | Medium | High | High | Anti-rollback monotonic counter; `eos_image_check_version()` | Planned |
| T-203 | Denial of Service | Corrupt stage-1 image prevents boot | Low | High | Medium | Recovery mode fallback; redundant boot control block | Mitigated |
| T-204 | Elevation of Privilege | Stage-1 runs with same privilege as stage-0 (no isolation) | Medium | High | Medium | MPU configuration at stage boundary (Phase 3); `eos_mpu_boot.h` | Planned |

### TB-3: Stage-1 → Application

| ID | STRIDE | Threat | Likelihood | Impact | Risk | Mitigation | Status |
|---|---|---|---|---|---|---|---|
| T-301 | Tampering | Attacker modifies application firmware in flash | Medium | Critical | High | CRC32 integrity check (Phase 1); SHA-256 + Ed25519 signature (Phase 2) | Partial |
| T-302 | Spoofing | Rollback to older firmware version with known vulnerabilities | Medium | High | High | Version comparison via `eos_image_check_version()`; monotonic counter (Phase 2) | Planned |
| T-303 | Information Disclosure | Unencrypted firmware reveals intellectual property | Medium | Medium | Medium | AES-256-GCM payload encryption (Phase 3); `EOS_IMG_FLAG_ENCRYPTED` | Planned |
| T-304 | Denial of Service | Both slots contain invalid images, device is bricked | Low | Critical | Medium | Recovery image in dedicated partition (16 KB); UART recovery protocol | Mitigated |
| T-305 | Repudiation | No audit trail of which firmware version was booted | Low | Medium | Low | Boot log records slot, version, verification result, reset reason | Mitigated |
| T-306 | Elevation of Privilege | Application gains ability to overwrite bootloader regions | Low | Critical | Medium | Flash write protection for bootloader regions (platform MPU/flash lock) | Planned |

### TB-4: UART Recovery Interface

| ID | STRIDE | Threat | Likelihood | Impact | Risk | Mitigation | Status |
|---|---|---|---|---|---|---|---|
| T-401 | Spoofing | Unauthorized party connects to UART and replaces firmware | Medium | Critical | High | Recovery authentication — challenge-response or shared secret (Phase 3) | Planned |
| T-402 | Tampering | Malformed UART packets corrupt flash or boot state | Medium | High | High | Packet validation; length bounds checking; CRC on write data | Partial |
| T-403 | Denial of Service | Flood UART with garbage to prevent legitimate recovery | Low | Medium | Low | Command timeout; watchdog feed during recovery; rate limiting | Partial |
| T-404 | Information Disclosure | INFO and LOG commands leak device configuration and boot history | Low | Medium | Low | Restrict INFO/LOG to authenticated sessions (Phase 3) | Planned |
| T-405 | Repudiation | Recovery operations not logged | Medium | Medium | Medium | Boot log records recovery entry, commands executed, exit reason | Planned |
| T-406 | Elevation of Privilege | FACTORY command erases all data without authorization | Medium | Critical | High | Require authentication before FACTORY; confirmation sequence | Planned |

### TB-5: Flash Storage

| ID | STRIDE | Threat | Likelihood | Impact | Risk | Mitigation | Status |
|---|---|---|---|---|---|---|---|
| T-501 | Tampering | Direct flash modification via physical access or glitch attack | Low | Critical | Medium | SHA-256 + signature verification at boot; flash write protection | Planned |
| T-502 | Tampering | Power loss during boot control block write corrupts metadata | Medium | High | High | Anti-tearing dual-write (primary + backup); CRC32 validation on load | Mitigated |
| T-503 | Information Disclosure | Flash contents readable via chip-off or debug interface | Low | High | Medium | Payload encryption with AES-256-GCM (Phase 3); debug lock | Planned |
| T-504 | Denial of Service | Flash wear-out from excessive erase/write cycles | Low | High | Low | Wear-leveling for boot control region; write coalescing | Monitored |

### TB-6: Debug Interface (JTAG/SWD)

| ID | STRIDE | Threat | Likelihood | Impact | Risk | Mitigation | Status |
|---|---|---|---|---|---|---|---|
| T-601 | Information Disclosure | Firmware and secrets extracted via debug probe | Medium | Critical | High | Platform debug lock (STM32 RDP Level 2, nRF APPROTECT); disabled in production | Planned |
| T-602 | Tampering | Runtime memory modified to bypass signature checks | Medium | Critical | High | Debug lock; glitch detection (platform-dependent) | Planned |
| T-603 | Elevation of Privilege | Debug interface used to execute arbitrary code | Medium | Critical | High | Debug lock in production; conditional compilation removes debug hooks | Planned |

### TB-7: Firmware Update Pipeline

| ID | STRIDE | Threat | Likelihood | Impact | Risk | Mitigation | Status |
|---|---|---|---|---|---|---|---|
| T-701 | Tampering | Man-in-the-middle modifies firmware image during OTA download | Medium | Critical | High | Application-layer TLS; Stage-1 verifies image signature before boot | Partial |
| T-702 | Spoofing | Attacker impersonates update server | Medium | High | High | Certificate pinning in application; image signed with Ed25519 key | Planned |
| T-703 | Repudiation | No record of who authorized a firmware update | Low | Medium | Low | Boot log records update source, version, timestamp | Planned |
| T-704 | Denial of Service | Repeated invalid update attempts exhaust flash erase cycles | Low | Medium | Low | Rate limiting on update requests; validate header before erase | Monitored |

---

## 5. Risk Summary

| Risk Level | Count | Examples |
|---|---|---|
| **Critical** | 0 | — |
| **High** | 12 | T-201, T-301, T-401, T-601, T-602, T-603, T-701 |
| **Medium** | 10 | T-204, T-303, T-304, T-306, T-402, T-405, T-501, T-503 |
| **Low** | 6 | T-103, T-305, T-403, T-404, T-504, T-704 |

> **Note:** No risks are rated Critical overall because the phased mitigation strategy addresses the highest-impact threats. Residual risk is High for threats dependent on Phase 2/3 implementation.

---

## 6. Mitigation Roadmap

### Phase 1 — Implemented

| Control | Threats Addressed |
|---|---|
| CRC32 integrity check on images | T-301 (partial) |
| Dual boot control block with CRC | T-502 |
| Version comparison API | T-302 (partial) |
| Boot log with reset reason | T-305 |
| Recovery mode with packet validation | T-402 (partial) |
| Redundant metadata (primary + backup) | T-203, T-304 |

### Phase 2 — Planned

| Control | Threats Addressed | Target |
|---|---|---|
| SHA-256 image hash | T-201, T-301, T-501 | v0.2.0 |
| Ed25519 signature verification | T-201, T-301, T-701 | v0.2.0 |
| Anti-rollback monotonic counter | T-202, T-302 | v0.2.0 |
| Key hash in image TLV | T-701, T-702 | v0.2.0 |
| Security counter in image header | T-202, T-302 | v0.2.0 |

### Phase 3 — Planned

| Control | Threats Addressed | Target |
|---|---|---|
| AES-256-GCM payload encryption | T-303, T-503 | v0.3.0 |
| Recovery authentication (challenge-response) | T-401, T-404, T-405, T-406 | v0.3.0 |
| Debug lock enforcement at boot | T-601, T-602, T-603 | v0.3.0 |
| MPU isolation between stages | T-204, T-306 | v0.3.0 |
| Flash write protection for bootloader | T-101, T-306 | v0.3.0 |

---

## 7. Risk Assessment Method

Consistent with [risk_register.md](compliance/risk_register.md):

- **Likelihood:** Low / Medium / High
- **Impact:** Low / Medium / High / Critical
- **Overall Risk:** Derived from likelihood × impact matrix:

| | Low Impact | Medium Impact | High Impact | Critical Impact |
|---|---|---|---|---|
| **High Likelihood** | Medium | High | High | Critical |
| **Medium Likelihood** | Low | Medium | High | High |
| **Low Likelihood** | Low | Low | Medium | Medium |

- **Status:** Open / Monitored / Partial / Planned / Mitigated / Closed

---

## 8. Review Schedule

| Activity | Frequency | Owner |
|---|---|---|
| Threat model review | Every major release | Security lead |
| Risk rating update | Quarterly | Project lead |
| New attack surface assessment | On architecture change | Development team |
| Penetration test (UART, flash) | Annually | External assessor |

---

## References

- [Security Model](security.md)
- [Risk Register](compliance/risk_register.md)
- [Architecture](architecture.md)
- [Key Lifecycle](key_lifecycle.md)
- [Secure Boot Chain](secure_boot_chain.md)
