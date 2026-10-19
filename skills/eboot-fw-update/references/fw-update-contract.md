# eBoot firmware-update contract (pinned reference)

Pinned from `include/eos_fw_update.h`, `include/eos_rollback.h`,
`docs/adr/ADR-020-install-path-verifies-signature-before-anti-rollback.md`,
and `docs/update-flow.md` at the commit that added
`skills/eboot-fw-update`. If the code changes any line of this
contract, this file and the SKILL.md discipline section change in
the same commit.

## Install API (`eos_fw_update_*`)

| Function | Contract |
|---|---|
| `eos_fw_update_begin(ctx, slot)` | Starts a session and **erases the target slot**; the slot must not be the active slot. |
| `eos_fw_update_write(ctx, data, len)` | Streams header → payload → authenticated TLV tail (`hdr.tlv_len` bytes after the payload). Bytes beyond the container are **rejected with an error**, never discarded with EOS_OK. |
| `eos_fw_update_bytes_wanted(ctx)` | Container bytes still accepted; 0 once complete, idle, or failed. While the header is arriving only the header remainder is known — re-ask after each write. Exists for block-framed transports (XMODEM) whose final block is padded. |
| `eos_fw_update_finalize(ctx, mode)` | Verifies integrity/signature, applies anti-rollback policy, updates boot control; `mode` is test or permanent. Ordering: see below. |
| `eos_fw_update_abort(ctx)` | Abandons the session and cleans up. |
| `eos_fw_update_progress(ctx)` / `eos_fw_update_get_state(ctx)` | 0–100 progress; state machine IDLE → HEADER → PAYLOAD → TLV → VERIFY → COMPLETE / ERROR. |

## Finalize ordering (ADR-020)

1. Signature verification over the signed header prefix
   (`EOS_IMG_SIGNED_LEN` — the first 92 bytes of the header)
   precedes anti-rollback evaluation.
2. The TLV security counter is read only after the prefix that
   binds it (`tlv_len`, `tlv_hash`) has been authenticated.
3. A signature failure is refused as `EOS_ERR_SIGNATURE`; the
   counter is never consulted.

This mirrors the master design's boot order (Verify Manifest →
Verify Image → Check Version Policy): authenticate first, then
apply version policy to what was authenticated.

## Anti-rollback (`eos_rollback_*`, threat model T-202)

- The image's counter is the `EOS_TLV_MIN_SEC_VER` TLV, **inside
  the signed area**. It is a security counter, not the firmware
  version. `eos_rollback_read_image_counter()` returns it (0 if
  no TLV present) and fails closed on malformed headers, tampered
  TLV areas, and erased flash.
- The device floor is backed by a monotonic counter (OTP):
  increments consume storage that cannot be reclaimed.
- `eos_rollback_verify(image_counter)` runs on every boot;
  returns `EOS_ERR_ANTI_ROLLBACK` below the floor. Boards with no
  counter return EOS_OK from verify (fail-closed there would be
  unbootable); `eos_rollback_get_device_counter()` reports
  `EOS_ERR_NOT_SUPPORTED` so provisioning can detect the case.
- `eos_rollback_stage()` records the booted image's counter;
  `eos_rollback_clear_staged()` discards it;
  `eos_rollback_commit()` raises the floor to the staged value —
  **only after the booted image is confirmed good**. Commit is
  idempotent, never lowers the floor, no-ops with nothing staged,
  refuses advances over `EOS_ROLLBACK_MAX_STEP` (16) as
  `EOS_ERR_INVALID`, and verifies the counter read back as
  expected (`EOS_ERR_GENERIC` otherwise).

## Rollback triggers (docs/update-flow.md)

| Trigger | Result |
|---|---|
| Unconfirmed test boot + reboot | boot_attempts incremented |
| boot_attempts ≥ max_attempts | Automatic rollback to the other slot |
| Watchdog reset during test boot | boot_attempts incremented |
| No valid image in active slot | Fallback to alternate slot |
| No valid image in either slot | Enter recovery mode |

Boot control is written to two flash sectors (primary + backup,
each CRC32-protected) so a power loss mid-write cannot tear the
metadata; on load, a CRC failure on the primary falls back to the
backup.

## Tests that pin this contract

- `tests/unit/test_fw_update.c` — finalize ordering
  (`test_finalize_bad_signature_below_floor_reports_signature`),
  below-floor refusal with a signed image
  (`test_write_streams_tlv_then_finalize_rejects_below_floor`),
  tampered-TLV refusal (`test_finalize_tampered_tlv_area_is_refused`),
  container-boundary rejection and `bytes_wanted` tracking,
  counter reads (`test_read_image_counter_*`, incl. tampered-TLV
  and erased-flash fail-closed cases, and counter == floor
  accepted).
- `tests/unit/test_rollback.c` — verify accept/reject, permissive
  behaviour without hardware, commit raise / no-op / idempotent /
  never-lowers / implausible-jump refusal / silent-fuse-failure
  detection, downgrade blocked after a confirmed update.
- `tests/unit/test_fw_update_test_sigs.py` — pins the committed
  `tests/vectors/fw_update_test_sigs.h` to the output of
  `tools/gen_fw_update_test_sigs.py` (eBoot has no Ed25519 signer
  in C; the generator signs the header prefixes the C tests
  build).
