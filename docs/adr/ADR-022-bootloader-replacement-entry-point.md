---
adr: 22
title: Privileged in-band stage-1 replacement entry point
status: Accepted
date: 2026-11-22
deciders: Architecture Council, eBoot maintainers
source: docs/design/bootloader-replacement-entry.md (merged #192, closes #193); foundation PR-EBT-3 (#191)
---

# ADR-022 — Privileged in-band stage-1 replacement entry point

## Context

Foundation PR-EBT-3 (#191) made the HAL allow-list refuse the bootloader
region unconditionally: every write through `eos_hal_flash_write()` /
`eos_hal_flash_erase()` to the region the board declares is refused with
`EOS_ERR_WRITE_PROTECTED`, and STM32F4 write-protects the region (stage-0 +
stage-1, sectors 0–3) in hardware via WRP option bytes. That closed the
fail-open shape, but it also left no way to replace a stage-1 in the field:
the only route was the UART recovery session, which needs physical access.
A device that cannot be updated in-band cannot receive a bootloader security
fix without a site visit.

The design note (`docs/design/bootloader-replacement-entry.md`, merged in
#192) specifies the entry point: stage-1 is replaceable in-band, stage-0 is
never replaced (16 KiB root of trust, sector 0), the image is verified twice
(once at staging, once by stage-0 before the copy), and the copy runs in
stage-0 through a single internal HAL entry gated by a one-shot RAM token.
This ADR records the implementation decisions the note leaves open, and the
places where the implementation had to read the note.

## Decision

### The transaction

1. `eos_bootloader_replace_request(EOS_SLOT_RECOVERY)` verifies the staged
   image (§3: structure, target identity, SHA-256 integrity, Ed25519
   signature, anti-rollback counter), writes the PENDING intent record
   into bootctl, and resets. A bad image is refused before anything is
   recorded.
2. On the next boot, the stage-0 executor (`eblr_bootloader_replace_execute()`,
   called from `jump_stage1.c` before stage-1 is verified or jumped to)
   re-verifies the staged image and cross-checks it against the intent
   record, lifts WRP, copies the staged payload into the stage-1 region,
   verifies the written bytes, re-applies WRP, clears the record, and
   resets.
3. The new stage-1's counter becomes the shared anti-rollback floor only
   after the new stage-1 completes a full boot decision
   (`eos_bootloader_replace_commit_counter()`, called from `stage1/main.c`).

### What the note left open, and what was chosen

- **Payload-only copy.** The staged image is header + payload + TLV in the
  recovery slot; the installed stage-1 is a raw binary (vector table first,
  no header). The copy writes only `hdr.image_size` payload bytes to the
  stage-1 base, and the step-7 verification re-hashes the installed region
  against `hdr->hash`. (Note §10 step 7.)
- **Token is a session, not a single write.** The note says the token is
  "cleared on first use". A token that died on the first flash write could
  never complete a multi-chunk copy, so one mint authorizes one copy
  (erase + all chunk writes) and is cleared when the copy ends, success or
  failure. The executor mints immediately before the copy and clears in
  every exit path.
- **Privileged erase added alongside privileged write.** The note names
  only `eos_hal_flash_write_bootloader()`; the copy also needs an erase of
  the stage-1 region, and the erase goes through the same token gate
  (`eos_hal_flash_erase_bootloader()`), scoped to the same region.
- **Commit counter source.** The installed stage-1 carries no header and
  the intent record is cleared before the new stage-1 boots, so neither
  survives to the commit call. The new stage-1's counter is read from the
  staged image in the recovery slot — only when the staged image fully
  verifies (including signature, so a forged staged image cannot raise
  the floor) AND its payload digest matches the installed stage-1. The
  call runs on every stage-1 boot after a boot decision; it is idempotent
  and a no-op when the recovery slot holds an unrelated image.
- **Target encoding.** `EOS_TLV_TARGET` (0x41) carries ASCII
  `"<board_id>:stage-1"`, no NUL terminator, e.g. `stm32f4:stage-1`. The
  board id comes from the new `eos_board_ops_t.board_id` field; a board
  with no id refuses replacement (`EOS_ERR_NOT_SUPPORTED`).
- **COPYING-boot triage.** A COPYING record means the copy may have been
  interrupted, so the installed stage-1 may be partial — and it has no
  header to verify. A COPYING boot whose staged image cannot be verified
  enters recovery directly, without trying to boot the installed stage-1.
  The PENDING boot keeps its already-installed fast path: if the installed
  bytes already equal the verified staged payload, WRP is re-applied, the
  record is cleared, and the new stage-1 boots with no erase and no write.
- **CRC-only refusal code.** A signed image without the
  `EOS_IMG_FLAG_HASH_SHA256` flag is refused with `EOS_ERR_INVALID` (a
  policy refusal, not a hash mismatch).
- **bootctl v1 migration.** The intent record lives in bootctl, whose
  version moves 1 → 2. A v1 copy that fails the v2 CRC is now tried as a
  v1 block and migrated (active slot, confirmed slot, log head, boot
  count carried; intent starts IDLE), so fielded devices do not lose
  their slot state on the first boot after the update.
- **WRP re-apply failure halts.** Per the note §5 table: after a copy,
  a failed re-apply leaves the new stage-1 unprotected, so stage-0
  halts with the record staying COPYING rather than booting or entering
  recovery.

### What was NOT implemented

- **Recovery-protocol trigger (§6.1).** The boot-flow side is wired: the
  executor runs on PENDING/COPYING records, and a COPYING boot with an
  unverifiable staged image enters recovery. But there is no new
  authenticated recovery command that stages a replacement image over
  UART and triggers the executor; the authenticated recovery session
  (`RCVR_CMD_*`) is unchanged. Follow-up work.
- **The §3 prerequisite: signature-based stage-1 verification in
  stage-0.** Stage-0 still verifies stage-1 against a build-time hash
  (`EBLDR_VERIFY_STAGE1` / `stage1_expected_hash`). Until that becomes a
  signature check, a replaced stage-1 is refused by the old stage-0's
  hash check on the next boot and the device enters recovery. The
  replacement therefore ships for devices whose stage-0 already verifies
  by signature; fielded hash-pinned stage-0 devices cannot use in-band
  replacement. This is the remaining field-deployment gate, and it is a
  separate change — the design note states it as a prerequisite, not as
  part of this implementation.

## Consequences

- Contract change: yes. New public API (`eos_bootloader_replace.h`), new
  TLV type (`EOS_TLV_TARGET` 0x41), new HAL ops and board fields
  (`flash_unprotect_bootloader`, `board_id`,
  `bootloader_protect_needs_reset`), bootctl format v2, new fail code
  (`EBLDR_FAIL_BTLDR_REPLACE` 0xBAD4).
- Architecture impact: yes. The allow-list now has exactly one privileged
  path, and it is structural: one call site (the stage-0 executor), one
  token, one region. Review must hold that shape — a second privileged
  call site, a token that survives a reset, or a privileged write outside
  the bootloader region would re-open the guard this change exists to
  keep closed.
- Stage-0 stays the executor, not a public API: there is no callable
  entry point from unprivileged code. The only caller-visible surface is
  `eos_bootloader_replace_request()` (stage-1 side, stages and records)
  and `eos_bootloader_replace_commit_counter()` (stage-1 side, commits).
- The fail-closed table (note §5) is pinned by
  `tests/unit/test_bootloader_replace.c` (host, mock STM32F4 board) and
  `tests/unit/test_stm32f4_bootloader_replace.c` (real STM32F4 WRP
  program path on host). The host suite also pins the §3 prerequisite
  gate behaviorally: after a replacement completes, the next boot enters
  recovery because the hash-pinned stage-0 refuses the new stage-1.
