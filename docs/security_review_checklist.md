# Security Review Checklist — eBootloader

> **Purpose:** Mandatory checklist for pull requests that modify eBootloader security-critical code.
>
> **Scope:** All changes to `core/`, `stage0/`, `stage1/`, `hal/`, `include/`, `tools/sign_image.py`, and any file touching crypto, image verification, boot control, or recovery.

---

## How to Use

Copy this checklist into the PR description. Check each item that applies. Items marked **[REQUIRED]** must be checked for every PR. Items marked **[IF APPLICABLE]** only need to be checked when the PR touches that area.

---

## 1. Cryptographic Operations

- [ ] **[REQUIRED]** All crypto function return codes are checked (no ignored `int` returns from `eos_crypto_*` functions)
- [ ] **[REQUIRED]** No use of `memcmp()` for comparing hashes, signatures, MACs, or other secret-derived values — use constant-time comparison (`eos_secure_memcmp()` or equivalent)
- [ ] **[IF APPLICABLE]** Hash computations use the streaming API (`eos_sha256_init` → `eos_sha256_update` → `eos_sha256_final`) for large data to avoid stack-allocated copies
- [ ] **[IF APPLICABLE]** Signature verification follows hash-then-verify order: integrity check before signature check
- [ ] **[IF APPLICABLE]** No custom crypto implementations — use `eos_crypto_boot.h` APIs or approved libraries
- [ ] **[IF APPLICABLE]** Key material is never logged, printed, or transmitted over UART (including in debug builds)

---

## 2. Buffer Safety

- [ ] **[REQUIRED]** All buffer sizes are validated before read/write operations — no unchecked `len` parameters
- [ ] **[REQUIRED]** No stack-based variable-length arrays (VLAs) — all stack allocations use compile-time constants
- [ ] **[REQUIRED]** Array indices are bounds-checked before access
- [ ] **[IF APPLICABLE]** Flash read/write operations validate address ranges against slot boundaries (`eos_hal_slot_addr()` / `eos_hal_slot_size()`)
- [ ] **[IF APPLICABLE]** UART receive operations use bounded length from `eos_hal_uart_recv()` — never trust length fields from incoming packets without validation
- [ ] **[IF APPLICABLE]** Image header field `image_size` is validated against slot size before any operations
- [ ] **[IF APPLICABLE]** String operations use length-bounded variants (`strncpy`, `snprintf`) — no `strcpy`, `sprintf`, `strcat`

---

## 3. Memory and Secret Handling

- [ ] **[REQUIRED]** Secrets (keys, hashes, intermediate crypto state) are zeroed after use with a volatile-qualified or compiler-barrier-protected memset to prevent dead-store elimination
- [ ] **[REQUIRED]** No heap allocation (`malloc`, `calloc`, `realloc`, `free`) — all memory is statically allocated or stack-allocated with fixed sizes
- [ ] **[IF APPLICABLE]** `eos_sha256_ctx_t` structures are zeroed after `eos_sha256_final()` completes
- [ ] **[IF APPLICABLE]** Signature buffers on the stack are zeroed before function return
- [ ] **[IF APPLICABLE]** No sensitive data stored in `eos_bootctl_t` reserved fields

**Recommended pattern for secret zeroing:**

```c
static inline void eos_secure_zero(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len--) *p++ = 0;
}
```

---

## 4. HAL Function Pointer Safety

- [ ] **[REQUIRED]** All `eos_board_ops_t` function pointers are NULL-checked before invocation in `eos_hal_*` convenience functions
- [ ] **[REQUIRED]** `eos_hal_get_ops()` return value is checked for NULL before dereferencing
- [ ] **[IF APPLICABLE]** New HAL functions added to `eos_board_ops_t` have corresponding NULL-safe wrappers in `eos_hal.h`
- [ ] **[IF APPLICABLE]** Board implementations (`boards/`) initialize all required function pointers — no uninitialized members in the ops struct

---

## 5. Flash and Metadata Integrity

- [ ] **[REQUIRED]** Boot control block writes use the anti-tearing sequence: compute CRC → erase primary → write primary → erase backup → write backup
- [ ] **[REQUIRED]** `eos_bootctl_save()` is used instead of raw flash writes for boot control updates
- [ ] **[IF APPLICABLE]** New persistent metadata fields include CRC coverage — `eos_bootctl_t.crc32` is recomputed after any field modification
- [ ] **[IF APPLICABLE]** Flash erase operations validate the target address is within the intended region (not overlapping bootloader or other protected regions)
- [ ] **[IF APPLICABLE]** Image write operations do not span slot boundaries
- [ ] **[IF APPLICABLE]** Power-loss safety is documented for any new multi-step flash update sequence

---

## 6. Image Header and Verification

- [ ] **[IF APPLICABLE]** `eos_image_parse_header()` validates magic number, header version, and header size before populating the output structure
- [ ] **[IF APPLICABLE]** Image verification follows the correct order: parse header → verify integrity → verify signature → check version
- [ ] **[IF APPLICABLE]** Anti-rollback check (`eos_image_check_version()`) is not bypassed for any code path
- [ ] **[IF APPLICABLE]** New image header fields are added to reserved space without changing struct layout or CRC offset
- [ ] **[IF APPLICABLE]** `sign_image.py` changes are tested with `--verify` flag against known-good test vectors

---

## 7. Recovery Protocol

- [ ] **[IF APPLICABLE]** New UART commands validate all packet fields (cmd, slot, len, offset) before processing
- [ ] **[IF APPLICABLE]** WRITE command validates offset + length does not exceed slot size
- [ ] **[IF APPLICABLE]** ERASE command targets only valid slot addresses
- [ ] **[IF APPLICABLE]** FACTORY command (0x09) requires confirmation sequence (not single-packet execution)
- [ ] **[IF APPLICABLE]** Recovery operations are logged to the boot log

---

## 8. Compliance and Headers

- [ ] **[REQUIRED]** All new source files (`.c`, `.h`) include the SPDX license header:
  ```c
  // SPDX-License-Identifier: MIT
  // Copyright (c) 2026 EoS Project
  ```
- [ ] **[REQUIRED]** All new header files include the ISO compliance comment:
  ```c
  // ISO/IEC 25000 | ISO/IEC/IEEE 15288:2023
  ```
- [ ] **[REQUIRED]** Include guards use the `EOS_<FILENAME>_H` pattern matching existing headers

---

## 9. Testing

- [ ] **[REQUIRED]** New code has corresponding unit tests in `tests/`
- [ ] **[REQUIRED]** Security-critical code paths have both positive (valid input) and negative (invalid input, boundary conditions) test cases
- [ ] **[IF APPLICABLE]** Crypto functions are tested against known-answer test vectors (e.g., NIST CAVP for SHA-256, RFC 8032 for Ed25519)
- [ ] **[IF APPLICABLE]** Flash operations are tested with simulated flash (mock HAL) including error injection (read failure, write failure, erase failure)
- [ ] **[IF APPLICABLE]** Boot control state machine changes are tested with all valid state transitions
- [ ] **[IF APPLICABLE]** Image verification is tested with corrupted headers, truncated payloads, wrong signatures, and version rollback attempts

---

## 10. Build and Toolchain

- [ ] **[REQUIRED]** No new compiler warnings introduced (`-Wall -Wextra -Werror`)
- [ ] **[REQUIRED]** Code compiles cleanly on all CI targets (ARM Cortex-M4, Cortex-M7, RISC-V RV32, host x86_64)
- [ ] **[IF APPLICABLE]** No new dependencies introduced without security review
- [ ] **[IF APPLICABLE]** `CMakeLists.txt` changes do not weaken compiler security flags (`-fstack-protector`, `-D_FORTIFY_SOURCE`, etc.)
- [ ] **[IF APPLICABLE]** Static analysis (cppcheck, clang-tidy) passes without new warnings

---

## 11. Documentation

- [ ] **[IF APPLICABLE]** New security-relevant APIs have Doxygen `@brief`, `@param`, and `@return` documentation
- [ ] **[IF APPLICABLE]** Security design decisions are documented in `docs/security.md` or relevant doc
- [ ] **[IF APPLICABLE]** Threat model (`docs/threat_model.md`) is updated if new attack surfaces are introduced
- [ ] **[IF APPLICABLE]** Architecture diagram (`docs/architecture.md`) is updated if boot flow changes

---

## Quick Reference: Common Mistakes

| Mistake | Correct Pattern |
|---|---|
| `if (memcmp(hash_a, hash_b, 32) == 0)` | `if (eos_secure_memcmp(hash_a, hash_b, 32) == 0)` |
| `memset(ctx, 0, sizeof(*ctx))` (may be optimized out) | `eos_secure_zero(ctx, sizeof(*ctx))` |
| `uint8_t buf[len]` (VLA on stack) | `uint8_t buf[MAX_BUF_SIZE]` (fixed-size) |
| `ops->flash_write(addr, data, len)` (no NULL check) | `if (ops && ops->flash_write) ops->flash_write(...)` |
| `eos_image_verify_signature(&hdr)` (return ignored) | `rc = eos_image_verify_signature(&hdr); if (rc != EOS_OK) return rc;` |
| `sprintf(buf, "version: %d", ver)` | `snprintf(buf, sizeof(buf), "version: %d", ver)` |

---

## References

- [Security Model](docs/security.md)
- [Threat Model](docs/threat_model.md)
- [Architecture](docs/architecture.md)
- [Key Lifecycle](docs/key_lifecycle.md)
