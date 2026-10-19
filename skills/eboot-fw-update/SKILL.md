---
name: eboot-fw-update
description: Drive or review an eBoot firmware update against its install and anti-rollback contract. Use when an agent needs to install an image through the eos_fw_update_* streaming API, reason about test-boot confirmation and rollback, check whether an image will be refused (signature, anti-rollback floor, tampered TLV), or write tests for any of that — e.g. before shipping an OTA flow, reviewing update code, or debugging a refused update.
---

# eBoot Firmware Update & Anti-Rollback

The update contract implemented by `core/fw_update.c` and
`core/rollback.c`, declared in `include/eos_fw_update.h` and
`include/eos_rollback.h`, and decided in
`docs/adr/ADR-020-install-path-verifies-signature-before-anti-rollback.md`.
The full OTA sequence (firmware-owned download, bootloader-owned
activation) is `docs/update-flow.md`. An agent that installs,
reviews, or tests an update follows this contract exactly — the
ordering rules below are security properties, not style.

## Quick start

Install is a streamed session into the **inactive** slot:

```c
eos_fw_update_ctx_t ctx;
eos_fw_update_begin(&ctx, EOS_SLOT_B);            /* erases target slot */
while (data_available) {
    /* Feed exactly the container: header, payload, then the
       authenticated TLV tail (hdr.tlv_len bytes). Bytes past the
       container are rejected, not discarded. On a block-framed
       transport (e.g. XMODEM padding), ask how much is wanted
       instead of pushing padding in: */
    uint32_t want = eos_fw_update_bytes_wanted(&ctx);
    eos_fw_update_write(&ctx, chunk, min(chunk_len, want));
}
eos_fw_update_finalize(&ctx, EOS_UPGRADE_TEST);   /* or EOS_UPGRADE_PERMANENT */
```

Boot the new image in test mode, self-test, then confirm —
an unconfirmed test boot rolls back (see `references/fw-update-contract.md`
for the trigger table):

```c
if (eos_fw_is_test_boot()) {
    if (self_test_passed())
        eos_fw_confirm_running_image();
    /* else: do not confirm — watchdog / attempt counter rolls back */
}
```

Run the tests that pin this contract (from the repo root):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DEBLDR_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
# the contract tests: tests/unit/test_fw_update.c,
# tests/unit/test_fw_update_sig.c, tests/unit/test_rollback.c
```

## What it answers

- **In what order is an image judged?** — Signature first, policy
  second (ADR-020): `eos_fw_update_finalize()` verifies the
  signature over the signed header prefix (`EOS_IMG_SIGNED_LEN`,
  the first 92 bytes) *before* any anti-rollback evaluation, and
  reads the TLV security counter only after the prefix that binds
  it (`tlv_len`, `tlv_hash`) has been authenticated. An image that
  fails signature verification is refused as `EOS_ERR_SIGNATURE`
  and its counter is never consulted.
- **What makes an image "too old"?** — Its security counter: the
  `EOS_TLV_MIN_SEC_VER` TLV inside the signed area, compared
  against the device floor by `eos_rollback_verify()`. The counter
  is **not** the firmware version — the floor is backed by OTP, so
  increments are irreversible and reserved for releases that close
  a vulnerability.
- **When does the floor move?** — Never at install or first boot.
  `eos_rollback_verify()` runs on every boot;
  `eos_rollback_stage()` records the booted image's counter;
  `eos_rollback_commit()` raises the floor **only after the booted
  image has been confirmed good**. Commit is idempotent, never
  lowers the floor, and refuses an advance larger than
  `EOS_ROLLBACK_MAX_STEP` (16) as `EOS_ERR_INVALID`.
- **What fails closed?** — A tampered TLV area is refused at
  finalize and at counter read; erased flash fails the counter
  read; bytes past the image container are rejected by
  `eos_fw_update_write()` on every chunk boundary.
- **What if the board has no monotonic counter?** —
  `eos_rollback_verify()` returns `EOS_OK` (failing closed would
  make such boards unbootable). Detect the case at provisioning
  time: `eos_rollback_get_device_counter()` returns
  `EOS_ERR_NOT_SUPPORTED` there.

## Discipline

1. **Authenticate first, then apply policy.** Never read, log, or
   branch on an image's security counter before its signature has
   verified — that is the ADR-020 ordering, and the tests enforce
   it (`test_finalize_bad_signature_below_floor_reports_signature`).
2. **A rollback test needs a genuinely signed image.** An unsigned
   image is refused as `EOS_ERR_SIGNATURE` before its counter is
   compared, so it exercises the signature check, not the floor.
   Test signatures are generated, never hand-made:
   `tools/gen_fw_update_test_sigs.py` emits
   `tests/vectors/fw_update_test_sigs.h`, pinned by
   `tests/unit/test_fw_update_test_sigs.py`. Regenerate and commit
   both together. (Direct counter-read tests use fixture-signed
   images for the same reason.)
3. **Verify and commit are separate steps.** Do not raise the
   floor when an image is accepted or first booted — that strands
   the device if the unproven image fails. Stage at boot, commit
   on confirmation.
4. **Never stream past the container.** Feed exactly
   header + payload + `tlv_len` TLV bytes; use
   `eos_fw_update_bytes_wanted()` on padded transports and re-ask
   after every write (while the header is arriving, only the
   header remainder is known).
5. **Target the inactive slot, boot in test mode.** `begin()`
   erases the target slot; finalizing with `EOS_UPGRADE_TEST` and
   confirming after self-test is the default path. Permanent
   upgrade without a test boot skips the only rollback window.
6. **Pin this contract.** The function semantics, error codes,
   and pinning tests are recorded in
   `references/fw-update-contract.md`; changing any of them is a
   reviewed change to the skill and the reference together.

## References

- `references/fw-update-contract.md` — the pinned contract: API
  semantics, finalize ordering, anti-rollback state machine,
  rollback triggers, and the tests that prove each property.
