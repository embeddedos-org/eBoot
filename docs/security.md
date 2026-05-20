# eBootloader Security Model

## Phased Security Approach

eBootloader uses an incremental security model. The idea is to start
with a correct, simple system and layer complexity only when needed.

## Phase 1: Minimum Viable Protection

Available now in the initial codebase.

### CRC32 Integrity Checks
- Every firmware image has a CRC32 in its header hash field
- The boot manager verifies CRC32 before jumping to any image
- Boot control blocks carry CRC32 to detect corruption

### Version Comparison
- Image headers include a version field (major.minor.patch)
- The `eos_image_check_version()` API enables anti-rollback policies
- Firmware services expose version info to the application

### Redundant Metadata
- Boot control block is stored in two independent flash sectors
- If the primary copy is corrupt, the backup is used
- Both copies are written on every metadata update

### Anti-Tearing Writes
- Write sequence: compute CRC → erase primary → write primary → erase backup → write backup
- Power loss at any point leaves at least one valid copy

## Phase 2: Production-Grade Protection

Planned for future releases.

### SHA-256 Image Hash
- Replace CRC32 with a full SHA-256 hash in the header
- `sign_image.py --method sha256` already generates SHA-256 hashes
- Firmware verification reads the hash and recomputes over the payload

### Ed25519 / ECDSA Signature Verification
- The image header has a 64-byte signature field
- `sig_type` field indicates the signature algorithm
- Public key is embedded in the stage-1 boot code (cannot be modified)
- Signature verification happens in `eos_image_verify_signature()`

### Anti-Rollback Counters
- A monotonic counter stored in a dedicated flash region
- Each firmware release increments the counter
- Older firmware versions cannot be installed

## Phase 3: Advanced Security

### Payload Encryption
- Images can be encrypted (AES-256-CBC or AES-256-GCM)
- Decryption key stored in hardware secure storage (OTP / eFuse)
- `EOS_IMG_FLAG_ENCRYPTED` flag in the image header

### Recovery Authorization
- Recovery mode requires authentication (shared secret or challenge-response)
- Prevents unauthorized firmware replacement via physical access

### Secure Boot Chain
- Stage-0 verifies stage-1 before jumping
- Stage-1 verifies application before jumping
- Full chain of trust from ROM to application

## Design Rules

1. **Do not start with maximum complexity.** A correct CRC-based boot
   path is better than a broken signature implementation.

2. **Security features should be additive.** Phase 1 code should work
   unchanged when Phase 2 features are added.

3. **Never store secrets in flash alongside firmware.** Use hardware
   secure storage (OTP, eFuse, secure enclave) for keys.

4. **Recovery mode is a security boundary.** Physical access to UART
   should require authentication in production builds.

---

## Implementation Status

### Phase 2 Feature Status

| Feature | Module | API | Status | Acceptance Criteria |
|---|---|---|---|---|
| SHA-256 image hash | `core/crypto_boot.c` | `eos_crypto_hash()`, `eos_crypto_verify_image()` | ✅ Implemented (FIPS 180-4) | Hash matches NIST CAVP test vectors; image verification rejects single-bit payload corruption |
| Ed25519 signature verification | `core/crypto_boot.c` | `eos_crypto_verify_signature()` | ✅ Implemented (RFC 8032) | Passes RFC 8032 §7.1 test vectors; rejects forged signatures; constant-time execution |
| Image signature check | `core/image_verify.c` | `eos_image_verify_signature()` | ✅ Implemented | Valid signatures accepted; invalid rejected with `EOS_ERR_SIGNATURE`; all return codes checked |
| Key hash in TLV | `include/eos_image.h` | TLV extension (planned) | 🔲 Not started | Key hash selects correct verification key slot; unknown key hash returns error |
| Anti-rollback counter | `include/eos_bootctl.h` | `eos_image_check_version()` | 🔶 API defined | Counter stored in OTP/flash; images with lower counter rejected; counter survives factory reset |
| Security counter in header | `include/eos_image.h` | Header `reserved` field allocation | 🔲 Not started | Counter field parsed from header; compared against device counter before boot |
| Dual key slots | `include/eos_signing_key.h` | Compiled-in key array | 🔲 Not started | Two key slots available; revoked slots skipped; rotation without bricking |

### Phase 3 Feature Status

| Feature | Module | API | Status | Acceptance Criteria |
|---|---|---|---|---|
| AES-256-GCM payload encryption | `core/crypto_boot.c` | Planned | 🔲 Not started | Encrypted images decrypted before verification; GCM tag validated; plaintext never stored in flash |
| Recovery authentication | `core/recovery.c` | Planned | 🔲 Not started | Unauthenticated UART commands rejected; challenge-response completes in < 5 seconds; brute-force rate limited |
| Debug lock at boot | `boards/*/board_init.c` | Platform-specific | 🔲 Not started | JTAG/SWD locked in production builds; debug lock applied before any secret processing; verified by probe test |
| MPU isolation between stages | `core/mpu_boot.c` | `eos_mpu_boot.h` | 🔲 Not started | Stage-1 cannot write stage-0 region; application cannot write bootloader regions; MPU fault handler logs violation |
| Flash write protection | HAL | Platform-specific | 🔲 Not started | Bootloader flash sectors marked read-only after stage-0 init; write attempts generate fault |
| Recovery audit logging | `core/recovery.c` | Planned | 🔲 Not started | All recovery commands logged with timestamp; log survives reset; log readable via LOG command |

### Status Legend

| Symbol | Meaning |
|---|---|
| ✅ | Implemented and tested |
| 🔶 | API defined, partial or stub implementation |
| 🔲 | Not started |

---

## Cross-References

| Document | Description |
|---|---|
| [Threat Model](threat_model.md) | STRIDE-based threat analysis with risk ratings for each trust boundary |
| [Key Lifecycle](key_lifecycle.md) | Ed25519 key generation, dual-slot support, rotation, revocation, and emergency response |
| [Secure Boot Chain](secure_boot_chain.md) | Complete chain of trust from ROM → stage-0 → stage-1 → application with verification steps |
| [mcuboot Comparison](mcuboot_comparison.md) | Design decisions relative to mcuboot — adopted patterns, intentional divergences |
| [Security Review Checklist](security_review_checklist.md) | PR review checklist for security-critical code changes |
| [Architecture](architecture.md) | Staged boot architecture, module map, flash memory map, boot flow |
| [Update Flow](update-flow.md) | OTA update sequence, rollback triggers, anti-tearing protection |
| [Risk Register](compliance/risk_register.md) | Project-wide risk register with likelihood, impact, and mitigation status |
