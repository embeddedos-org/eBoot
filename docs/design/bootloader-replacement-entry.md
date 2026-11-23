# Design note — privileged in-band bootloader-replacement entry point

| Field | Value |
|---|---|
| Status | Accepted — implemented in ADR-022 (PR #200, merged 2026-11-23; privileged range narrowed to stage-1 in the 2026-11-23 follow-up, §12) |
| Date | 2026-11-21 |
| Deciders | eBoot maintainers |
| Follow-up of | Foundation PR-EBT-3, eBoot #191 (merged 2026-11-20) |
| Size | S — docs only. **No code lands in the PR that carries this note; code follows after review.** |
| Seam + journey stage | The bootloader cannot be replaced in the field today — a stage-1 fix requires an external probe. Stage: integrating/shipping (safe in-field bootloader replacement). |
| Contract-change | No — this note proposes a future contract; it changes none. |
| Architecture-impact | Yes — a future contract change is designed here. |

If accepted, record as ADR-022 in `docs/adr/`.

## 0. Problem

#191 closed the hole that let any flash-write path overwrite the bootloader:
the HAL allow-list (`hal/hal_core.c`) refuses every write/erase intersecting
the board-declared bootloader region with `EOS_ERR_WRITE_PROTECTED`, and on
STM32F4 the region (stage-0 + stage-1, sectors 0–3) is hardware write-protected
via WRP option bytes. The header of `include/eos_bootloader_protect.h` states
the consequence plainly:

> There is no bootloader self-update path in the tree today, so the allow-list
> refuses the region unconditionally: a future self-update must go through an
> explicit privileged entry point, not around this guard.

This note designs that entry point. The need is real: the architecture already
treats the two stages asymmetrically — stage-0 is the minimal root of trust
(16 KiB) that "can be locked in flash and rarely updated, while stage-1 can be
updated more frequently with new boot policies"
(`docs/mcuboot_comparison.md` §4.3). Today "more frequently" still means "with
a debug probe". A stage-1 bug fix, a boot-policy change, or a key-rotation
delivery that touches stage-1 cannot reach a fielded device without one.

## 1. Scope and non-goals

- **Stage-1 is replaceable in-band. Stage-0 is not, ever.** Stage-0
  (STM32F4: `0x08000000`, 16 KiB, sector 0) is the immutable root of trust.
  It is the code that verifies stage-1, enters UART recovery, and applies the
  debug lock and WRP. Replacing it in-band has no fallback: a power cut
  mid-copy leaves no code that can recover the device except the ROM
  bootloader or an external probe. Stage-0 changes ship only via external
  flashing. This is the single decision everything in §6 follows from.
- Only **stage-1** (STM32F4: `0x08004000`, 48 KiB, sectors 1–3) is the
  replacement target. "Bootloader region" below means the stage-1 part of it.
- The note designs the entry point, the verification gate, the transaction,
  and the recovery path. It lands no code, no HAL op, no new TLV type, and no
  bootctl field. Proposed names and layouts in §9 are the review surface, not
  an API commitment.
- Out of scope: replacing the application image (the existing `eos_fw_update`
  A/B flow already does that), encrypted stage-1 images (encryption is a
  separate decision; a stage-1 image is verified, not decrypted, by this
  path), and any transport for delivering the candidate image (§7 makes the
  gate transport-agnostic).

## 2. Decision 1 — entry point and privilege boundary

### Decision

There is **no callable entry point from unprivileged code**. The replacement
is a boot-flow state, entered only by stage-0, on reset, when a valid
pending-replacement record exists in the boot control block. The application
can only *request* a replacement; it can never *perform* one.

Concretely, the boundary has three sides:

1. **Request side (stage-1 / application).** The application, or stage-1's
   own update flow, stages a complete candidate stage-1 image into the
   recovery slot (`EOS_SLOT_RECOVERY`) through the existing streaming update
   path, then writes a replacement-intent record into bootctl (§9) and
   resets. The application never writes the bootloader region and never
   calls any replacement function — the HAL allow-list refuses it, exactly
   as #191 intended.
2. **Execution side (stage-0 only).** On the next boot, stage-0 — running
   privileged, interrupts disabled, before any application code and before
   jumping to stage-1 — sees the pending record and runs the replacement
   executor: re-verify the staged image (§3), lift WRP, copy, re-apply WRP,
   verify the written bytes, clear the record, reset. The executor is a
   stage-0-internal function, not a public-header API.
3. **The single exemption.** The HAL allow-list keeps refusing the region
   for every caller. The executor reaches the region through one separate
   internal HAL entry (proposed name `eos_hal_flash_write_bootloader()`),
   guarded by a one-shot RAM authorization token: stage-0 sets the token
   only after the staged image has fully verified, the token is consumed by
   the copy and cleared on first use, and it never persists across reset
   (RAM does not survive one). C cannot enforce caller identity, so the
   enforcement is structural — one call site, one token, code review — not a
   flag any caller can set. No other code path sets the token.

### Gates on the call

The executor runs only when **all** of these hold; any failure refuses the
replacement (see §5 for the fail-closed table):

- a pending-replacement record exists, is CRC-valid, and names a staging
  slot that is not the bootloader region;
- the staged image passes the full verification of §3, re-run in stage-0;
- the board declares a bootloader region **and** provides the protect
  primitive (`flash_protect_bootloader` / status); a board with a region but
  no primitive reports `EOS_ERR_NOT_SUPPORTED` and the replacement is
  refused — the same fail-closed signal #191 established, never a silent
  success;
- no replacement is already in progress (record state machine, §9).

### Rejected alternatives

- **An application-callable API (e.g. an SVC or runtime-service call the app
  invokes to rewrite the bootloader).** Rejected: it hands a flash-write
  primitive for the root of trust to the least-trusted code on the device.
  A compromised application could then replace stage-1 with a signed-but-old
  image (if it also bypassed the counter) or, worse, brick the fleet. The
  request/execute split keeps the dangerous operation in code that runs
  before the application exists.
- **UART recovery `WRITE` to bootloader addresses.** Rejected: recovery
  writes through slot addresses today, and the allow-list refuses the region
  for the same reason it refuses everything else. Recovery *does* take part
  in this design, but only as a second trigger for the same verified-copy
  executor (§6), never as a raw write path.
- **A persistent "replacement authorized" flag in bootctl.** Rejected: a
  flag that survives reset and can be set by any writer of bootctl is an
  authorization that outlives the verification it was based on. The token is
  RAM-only and is minted fresh, after verification, on the boot that
  performs the copy.
- **Lifting WRP once and leaving it lifted for convenience.** Rejected:
  WRP is re-applied by the same transaction that lifted it, and verified by
  read-back before the record is cleared. A device that boots with its
  bootloader region writable has lost #191.

## 3. Decision 2 — signed-image verification before ANY flash write

### Verification order

The order extends ADR-020 (signature before anti-rollback in the install
path) to the replacement path. Nothing is written to the bootloader region —
not one sector erased — until every step passes, in this order:

1. **Structure.** Parse the header at the staging address
   (`eos_image_parse_header()`): magic `EOSI`, `hdr_version == 2`, and
   header + payload + TLV sizes fit the staging slot, overflow-checked with
   the same discipline as `rollback.c`'s `slot_remain_from()`. A malformed
   header is refused as `EOS_ERR_INVALID`.
2. **Target identity.** The image must declare, in authenticated fields, that
   it *is* a stage-1 image *for this board*:
   - `load_addr` must equal the board's stage-1 base
     (`flash_base + app_vector_offset`; STM32F4: `0x08004000`) and
     `entry_addr` must lie inside the stage-1 region. Both fields are inside
     the signed prefix, so they cannot be altered without breaking the
     signature.
   - A TLV target entry (proposed type `EOS_TLV_TARGET`, value 0x41, next to
     `EOS_TLV_DEPENDENCY` 0x40; value binds board identity + stage marker
     "stage-1") must match this board. `load_addr` alone is not enough: two
     boards can share an address map. The TLV entry is covered by
     `tlv_hash`, which is inside the signed prefix.
   - A stage-1 image whose `load_addr` points at the application slots, or
     an application image staged by mistake, is refused here — before any
     signature work is spent on it.
3. **Integrity.** SHA-256 over the payload must match `hash[]`
   (`EOS_IMG_FLAG_HASH_SHA256` is **required** for a bootloader replacement;
   a CRC-only image is refused). Integrity is checked before signature so a
   corrupted image is rejected cheaply — the same hash-then-verify pattern
   `docs/mcuboot_comparison.md` §2.2 records.
4. **Signature.** Ed25519 over the 92-byte signed prefix
   (`EOS_IMG_SIGNED_LEN`), verified against the keystore active key
   (`eos_image_verify_signature()`, the single policy point). `sig_type`
   must be `EOS_SIG_ED25519`: `NONE` / `CRC32` / `SHA256` / unknown types
   are refused as `EOS_ERR_SIGNATURE`, exactly as the install path does
   since #104. A revoked key is refused — revocation is checked at verify
   time (`eos_keystore`, OTP revoke byte, #189), not only at boot.
5. **TLV binding, then anti-rollback.** `tlv_len` / `tlv_hash` are inside
   the signed prefix, so the TLV area is authenticated before it is read
   (ADR-020 step 2). Only then is `EOS_TLV_MIN_SEC_VER` read and compared
   against the device floor with `eos_rollback_verify()` (§4). An image that
   fails signature verification is refused as `EOS_ERR_SIGNATURE` and its
   counter is never consulted (ADR-020 step 3).

### Where verification lives

**Twice: in stage-1 at staging time, and again in stage-0 before the copy.**

- Stage-1 verifies when the image is staged, so a bad image is rejected
  early with a useful log and the intent record is never written.
- Stage-0 **re-verifies everything from the staged image** before the copy.
  Stage-0 is the trust anchor and stage-1 is the code being replaced: a
  compromised stage-1 must not be able to vouch for its own successor.
  Trusting stage-1's verdict would make the whole gate depend on the code
  under replacement. This costs a second Ed25519 verification at boot; it is
  non-negotiable.

This is feasible without new link dependencies: stage-0 already links
`eboot_core`, which contains `core/ed25519_verify.c`,
`core/image_verify.c`, `core/keystore.c`, and `core/rollback.c`. The
implementation risk to measure is stage-0's 16 KiB size budget, not a new
dependency — flagged as a review question in §11.

### Prerequisite: stage-0 must verify stage-1 by signature, not by build-time hash

This is the gate the whole note stands on, and it is a **prerequisite code
change, not part of the replacement implementation**:

Today, under `EBLDR_VERIFY_STAGE1` (default ON), stage-0 verifies stage-1 by
SHA-256 against a hash embedded at build time
(`stage0/jump_stage1.c`, `tools/embed_stage1_hash.py`). A build-time hash can
never match a field-replacement image, so **in-band replacement is impossible
until stage-0 verifies stage-1 by Ed25519 signature against the keystore
instead**. That migration is already the documented design:
`docs/secure_boot_chain.md` §2.2 specifies the Phase-2 stage-0 flow as parse
header → integrity → `eos_image_verify_signature()` → version check. The code
has not caught up with the document.

Consequences, stated plainly:

- The replacement entry point ships only after the signature-based stage-1
  verification lands in stage-0 and is itself deployed.
- Fielded devices whose stage-0 pins stage-1 by build-time hash **cannot**
  use in-band replacement. They need a one-time external stage-0 update
  (probe / ROM bootloader) to a signature-verifying stage-0 first. There is
  no in-band path around this: the hash-pinned stage-0 would refuse the new
  stage-1 and enter recovery.
- The replacement executor reuses exactly that signature verification — one
  verify path for "may this stage-1 boot" and "may this stage-1 be
  installed", so the two can never disagree.

### Rejected alternatives

- **Verify once, in stage-1, and trust it in stage-0.** Rejected: see above
  — the code under replacement cannot vouch for its successor.
- **Verify the staged image's hash against a hash carried in the intent
  record.** Rejected: the record is written by stage-1, so it proves
  nothing stage-1 did not already claim. The record carries the expected
  digest as a *cross-check* (§9), not as the verification itself.
- **Allow CRC-only stage-1 images.** Rejected: CRC proves the payload
  matches a hash the same unauthenticated source supplied. A bootloader
  image gets the strongest integrity check the format offers, unconditionally.

## 4. Decision 3 — anti-rollback interplay

The replacement image's security counter is the same counter the application
update path uses, and it composes with the OTP-backed monotonic counter
(#181) and the anti-rollback fail-closed rule (#177) as follows:

- **One device floor, shared.** The device has one OTP-backed floor
  (`eos_hal_monotonic_read()`); the image carries one
  `EOS_TLV_MIN_SEC_VER`. The replacement image's counter is compared with
  `eos_rollback_verify()` after signature verification (§3 step 5). A
  counter below the floor is refused as `EOS_ERR_ANTI_ROLLBACK`. A board
  with no monotonic counter returns `EOS_ERR_NOT_SUPPORTED` and the
  replacement is refused — fail-closed, per #177. The only exception is the
  existing `EBLDR_ALLOW_NO_ROLLBACK_COUNTER` build, which is bring-up /
  host / sim only and can never be selected by a release-shaped build of a
  real board.
- **Verify now, commit only after the new stage-1 proves itself.** The
  stage/commit split (`eos_rollback_stage()` /
  `eos_rollback_commit()`) is preserved. The counter is *verified* at
  staging and again in stage-0, *staged* when the copy is authorized, and
  *committed* only after the new stage-1 has completed a full boot decision
  — scanned the slots, selected a verified application image, and is about
  to jump to it. Committing earlier would raise the floor for an image that
  has not yet proven it boots; if it then failed, the floor would already
  exclude the images it replaced. If power is lost before the commit, the
  new stage-1 still boots (its counter is ≥ the floor) and the next
  successful boot commits. Commit is idempotent, never lowers the floor, and
  refuses an advance over `EOS_ROLLBACK_MAX_STEP` (16) — the existing
  guards in `core/rollback.c` apply unchanged.
- **Counter bumps are for security events, not for every release.** A
  bootloader release bumps `MIN_SEC_VER` only when it closes a
  vulnerability or rotates keys (the same rule as `docs/key_lifecycle.md`
  §6.3). A routine stage-1 feature release reuses the current counter value
  (equal is accepted: counter ≥ floor). Reason: the STM32F4 OTP counter is a
  unary fuse bitmap — 4 bytes at OTP offset `0x144`, **32 steps total**,
  one fuse per increment, irreversible. Every bump is spent from the same
  32-step budget the application release train draws on. A release train
  that bumps on every build exhausts the device's anti-rollback capacity in
  a year of weekly releases.
- **Key rotation composes, it does not bypass.** If the replacement image is
  signed with a key that has since been revoked (OTP revoke byte, #189),
  signature verification fails and the replacement is refused — even if the
  counter is current. Revocation and anti-rollback are independent gates;
  passing one never excuses the other.

### Rejected alternatives

- **A separate bootloader-only counter.** Rejected: it would cost scarce OTP
  bits for a second floor, split the device's security epoch in two, and
  require a second TLV type plus a second verify path for the same property
  ("is this image current?"). One floor, one epoch, one budget — and the
  budget is managed by the bump discipline above, not by duplicating the
  mechanism.
- **Committing the counter before the copy, so a failed replacement cannot
  be retried with the old image.** Rejected: it inverts the stage/commit
  split for no benefit. The old stage-1 is overwritten by the copy anyway;
  the counter protects *future* installs and boots, and raising it before
  the new image has proven itself only narrows the fleet's options if the
  new image is bad.

## 5. Decision 4 — fail-closed rules

No valid signed image → no write, no boot change. The table is the contract;
every row ends with the device still booting the old stage-1 (or in recovery,
where noted) and the intent record cleared or left for the next boot to
dispose of:

| Failure | Result | Boot outcome |
|---|---|---|
| No staged image / staging slot empty | Refuse; log; clear intent | Old stage-1 boots |
| Malformed header / sizes do not fit | `EOS_ERR_INVALID`; clear intent | Old stage-1 boots |
| `load_addr` / TLV target mismatch | Refuse; log; clear intent | Old stage-1 boots |
| Payload hash mismatch | `EOS_ERR_CRC`; clear intent | Old stage-1 boots |
| Signature invalid / wrong `sig_type` / no usable key / revoked key | `EOS_ERR_SIGNATURE` (or `EOS_ERR_KEY`); clear intent | Old stage-1 boots |
| Counter below floor | `EOS_ERR_ANTI_ROLLBACK`; clear intent | Old stage-1 boots |
| No monotonic counter on board | `EOS_ERR_NOT_SUPPORTED`; clear intent | Old stage-1 boots |
| Board declares region but has no protect primitive | `EOS_ERR_NOT_SUPPORTED`; clear intent | Old stage-1 boots |
| WRP lift fails | Halt (fatal, consistent with `stage0/hw_init_minimal.c`: a production build treats a non-`EOS_OK` protection result as fatal — booting while believing itself protected is the fail-open shape #191 exists to remove) | Halt; external recovery |
| Copy write/read-back mismatch | Halt; record stays `COPYING` so the next boot knows the copy started (§6) | Recovery (stage-0 intact) |
| WRP re-apply fails after a good copy | Halt; do not boot the new stage-1 unprotected | Recovery (stage-0 intact; recovery re-stages and the transaction repeats) |
| Power loss before the copy starts | Nothing changed | Old stage-1 boots; stale intent cleared on next boot |
| New stage-1 fails to boot after install | Stage-0 verification fails; counter never committed (§4) | Recovery (stage-0 intact) |

Two rules cut across the table:

- **Invalid or missing image handling is always "refuse and clear".** The
  intent record is not retried, not queued, and not partially honoured. A
  failed replacement is a non-event for the running device: the operator
  stages a correct image and requests again.
- **Nothing in this path ever writes the bootloader region speculatively.**
  Erase of a stage-1 sector happens only after the staged image has fully
  verified in stage-0 on that same boot. There is no "erase first, verify
  while writing" shape.

## 6. Decision 5 — recovery path if interrupted mid-write

The invariant from §1 does the work: **stage-0 is never replaced in-band, so
any interruption leaves stage-0 intact, and stage-0 plus UART recovery can
always re-receive a stage-1 image.** The recovery path is not a second
mechanism bolted on afterwards; it is the same verified-copy executor with a
second trigger.

### Interruption taxonomy

| Interruption point | Device state after | Recovery |
|---|---|---|
| During staging (image incomplete in recovery slot) | Old stage-1 intact; staged bytes unverifiable | Next boot: verification fails, intent cleared, old stage-1 boots. Re-stage and retry. |
| After intent written, before WRP lift | Old stage-1 intact; intent `PENDING` | Next boot: stage-0 re-verifies and proceeds, or refuses and clears if the image no longer verifies. |
| During WRP lift (option-byte program) | Old stage-1 intact; WRP state unknown | Next boot: stage-0 re-applies protection idempotently (already the per-boot behaviour in `hw_init_minimal.c`); if the record is still `PENDING` and the image verifies, the transaction proceeds. |
| **During the copy (stage-1 region partially written)** | **Stage-1 corrupt; stage-0 intact** | Next boot: stage-0's stage-1 verification fails → **UART recovery**. The operator sends a known-good stage-1 image over the authenticated recovery session (§6.1); stage-0 verifies and copies it through the same executor. |
| During WRP re-apply | New stage-1 written; WRP state unknown | Next boot: stage-0 re-applies WRP idempotently; if apply fails, halt (fail-closed, §5). Stage-1 itself is already verified good. |
| During counter commit | New stage-1 installed and booting; floor unchanged | Commit retried on the next successful boot; idempotent (§4). |
| During the post-copy reset | New stage-1 written, record possibly uncleared | Next boot: stage-0 verifies the new stage-1; if it verifies and no copy is in progress, the stale record is cleared and boot proceeds. |

### 6.1 Recovery reuses the executor — it does not bypass it

UART recovery (`core/recovery.c`, 9-command protocol) runs from stage-0
(`stage0/recovery_entry.c`), so it survives stage-1 corruption by
construction. To repair a corrupted stage-1 through recovery:

1. The operator opens an **authenticated** recovery session. `WRITE`,
   `ERASE`, `VERIFY`, and `BOOT` already require HMAC-SHA256
   challenge-response authentication, with a 5-failure lockout
   (`RCVR_MAX_AUTH_FAILS`). An unauthenticated UART session must never be
   able to trigger a bootloader rewrite — that would hand the entry point to
   anyone with a serial cable.
2. The image is received into the recovery slot, exactly as a normal
   recovery write.
3. Stage-0 runs the **same** verification (§3) and the **same** verified
   copy into the stage-1 region. There is one privileged copy path and two
   triggers (boot-time pending record; authenticated recovery session), not
   two copy implementations that can drift apart.

### Rejected alternatives

- **A/B bootloader regions (two stage-1 copies, flip a pointer).**
  Rejected: it doubles the bootloader's 64 KiB flash footprint, complicates
  the vector table and WRP layout (which sectors are "the bootloader" would
  change per boot), and still needs a tie-breaker record for the
  interrupted-flip case. The UART recovery path already provides the
  fallback an A/B scheme would buy, at zero extra flash.
- **MCUboot-style swap of stage-1.** Rejected for the same reasons eBoot
  already rejected swap for applications (`docs/mcuboot_comparison.md`
  §3.1): 3× write amplification, a dedicated scratch area, and a complex
  swap state machine with resume-on-interrupt — all to solve a problem the
  intact stage-0 + recovery path already solves.
- **Replacing stage-0 in-band with a "recovery copy" of stage-0.** Rejected:
  there is nowhere to put the recovery copy that is itself protected from
  the same failure, and the scheme collapses into "stage-0 is really two
  stage-0s", doubling the most sensitive code on the device. Stage-0 is
  small (16 KiB) precisely so it rarely needs to change; when it does, an
  external flashing step is the honest price.
- **External probe as the only recovery.** Rejected: it defeats the purpose.
  A fielded device whose stage-1 update was interrupted mid-copy must be
  recoverable over the channel the operator already has (UART), not only
  over a channel that requires physical access.

## 7. Decision 6 — Zephyr equivalent: MCUboot + mcumgr/SMP

**State: ADAPT** (reference pattern; EoS deliberately differs).

The Zephyr equivalent is **MCUboot** (the bootloader) plus **mcumgr/SMP**
(the management protocol, including its image-management group for in-band
DFU). The pattern to adopt from it is the shape of a safe in-band update:

> stage the candidate in a secondary slot → verify signature and security
> counter before activation → test-boot → confirm → roll back on failure.

That shape is exactly what §3–§6 implement, and the note adopts it
deliberately: it is the proven pattern, and reusing its shape (rather than
inventing a new transaction) is what makes the design reviewable against a
known-good reference.

**Where EoS deliberately differs, and why:**

1. **The target is the bootloader itself.** In the Zephyr model, mcumgr
   image management updates the *application*; MCUboot itself is not
   updated in-band — updating MCUboot requires external flashing. EoS's
   in-band *stage-1* replacement therefore has no direct MCUboot equivalent:
   the pattern is adapted, the self-update mechanism is EoS-new. The note
   does not claim Zephyr does this; it claims the *pattern* transfers.
2. **No MCUboot dependency, no swap.** eBoot keeps its own image format
   (156-byte header v2, Ed25519 over the 92-byte signed prefix), its own
   keystore (dual slots, OTP revoke), its own OTP-backed counter, and its
   direct-write A/B application model. MCUboot's swap-based upgrade was
   already rejected for applications (`docs/mcuboot_comparison.md` §3.1)
   and is rejected again for stage-1 in §6. Adopting MCUboot would also
   collapse eBoot's three-stage design (ROM → stage-0 → stage-1 → app),
   whose point is a minimal, rarely-updated root of trust — the property
   this note relies on.
3. **Verification is keystore-based, not TLV-key-hash-based.** MCUboot
   selects among keys by a key hash carried in the image TLV; eBoot verifies
   against the keystore's active key and checks revocation in OTP. The
   difference is deliberate: the keystore is the device's own state, and a
   revoked key must fail even if the image names it correctly.
4. **Transport is separate from the gate.** mcumgr/SMP is a transport *and*
   a management protocol. In this design the transport that delivers the
   staged image is interchangeable — OTA via the application, UART recovery,
   or a future mcumgr/SMP transport that lands bytes in the recovery slot —
   while the verification gate (§3) is identical for all of them. A future
   proposal may add an SMP transport for staging; it must not re-implement
   or bypass the gate.

## 8. Decision 7 — future-proof by design

No EoS contract, type, protocol, or format defined by this note carries a
built-in expiry that a wider field, a version bump, or an algorithm swap
would have avoided:

- **No 32-bit time, and no time at all.** The replacement record, the intent
  flow, and the transaction carry no wall-clock timestamp. Ordering comes
  from the monotonic security counter (`uint32_t`, only ever rises) and from
  boot state, never from a clock. There is no "valid until", no expiry
  field, and no 32-bit `time_t` anywhere in the proposed contracts.
- **Versioned record, fail-closed on unknown versions.** The intent record
  (§9) carries a format version; a future stage-0 that meets a version it
  does not understand refuses the replacement rather than parsing it
  optimistically. The version field is `uint16_t`, not a single bit.
- **No fixed sizes in the contract.** Image sizes are `uint32_t` and
  overflow-checked; the staging slot is named by slot id, not by address;
  the stage-1 region is read from the board's declared region, not
  hard-coded. A future board with a larger stage-1 needs no contract change.
- **Crypto agility.** The signature algorithm is identified by the image's
  `sig_type` field (`eos_sig_type_t`), not hard-coded in the replacement
  flow, and verification goes through `eos_image_verify_signature()`, the
  single policy point. Ed25519 is required *today* by that policy point; a
  future algorithm lands there, and the replacement flow is unchanged. Key
  rotation rides the existing dual-slot keystore.
- **No silent ceilings.** The STM32F4 OTP counter's 32-step budget is a
  *board limitation*, recorded as such in §4 — it is not a contract limit.
  The note requires future boards to size their counter store for the
  device's service life, and the contract itself assumes only "monotonic,
  `uint32_t`, never lowers". If a board's counter store ever exhausts, the
  failure is a loud `EOS_ERR_NOT_SUPPORTED`-style refusal at provisioning,
  not a silent wrap.

## 9. Proposed contracts (review surface — not landed)

### 9.1 Replacement-intent record

Stored in the boot control block (`eos_bootctl_t`), reusing its existing
dual-copy CRC anti-tearing mechanism (primary + backup sectors; STM32F4:
`0x080C0000` / `0x080E0000`) rather than inventing a new persistent store.
Proposed shape:

```c
#define EOS_BTLDR_REPLACE_MAGIC  0x52504C43u  /* "RPLC" */

typedef enum {
    EOS_BTLDR_REPLACE_IDLE     = 0,  /* no replacement requested */
    EOS_BTLDR_REPLACE_PENDING  = 1,  /* staged + verified by stage-1, awaiting stage-0 */
    EOS_BTLDR_REPLACE_COPYING  = 2,  /* WRP lifted, copy started; stage-1 may be partial */
} eos_bootloader_replace_state_t;

typedef struct {
    uint32_t magic;              /* EOS_BTLDR_REPLACE_MAGIC */
    uint16_t version;            /* 1; unknown versions are refused */
    uint16_t state;              /* eos_bootloader_replace_state_t */
    uint32_t staging_slot;       /* eos_slot_t — EOS_SLOT_RECOVERY */
    uint8_t  image_sha256[32];   /* expected digest of the staged image (cross-check) */
    uint32_t image_counter;      /* expected EOS_TLV_MIN_SEC_VER */
    uint32_t image_size;         /* expected total bytes (header + payload + TLV) */
    uint32_t reserved[4];        /* extension space, zero */
} eos_bootloader_replace_intent_t;
```

Rules: the record is written only by stage-1 after the staged image has
verified; it is cleared by stage-0 when the transaction completes or is
refused. `COPYING` is the only state in which stage-1 may be partial — a
boot that finds `COPYING` and a failing stage-1 verification goes to
recovery, never to a jump. A `PENDING` record with a failing staged image is
cleared and the old stage-1 boots.

### 9.2 Proposed API sketch (design only)

```c
/* Stage-1 side — request only. Verifies the staged image, writes the
 * intent record, resets. Never touches the bootloader region. */
int eos_bootloader_replace_request(eos_slot_t staging_slot);

/* Stage-0 side — internal, no public header. Re-verifies the staged image,
 * lifts WRP, copies, re-applies WRP, verifies written bytes, clears the
 * intent, resets. The only caller of the privileged HAL write. */
int eblr_bootloader_replace_execute(const eos_bootctl_t *bctl);
```

### 9.3 Proposed HAL addition

One nullable op, mirroring the existing protect pair:

```c
/* Lift the bootloader region's hardware write protection (e.g. re-program
 * WRP option bytes). Takes effect at the next reset on parts whose option
 * bytes reload on reset (STM32F4). A board without the primitive reports
 * EOS_ERR_NOT_SUPPORTED and replacement is refused on that board. */
int (*flash_unprotect_bootloader)(void);
```

Whether protection changes take effect immediately or at next reset is a
board property the executor reads from the HAL; the transaction state
machine (§10) is written for the reset-required case and collapses to a
single boot where the HAL reports immediate effect. No board is guessed at:
a board that declares no region, or no unprotect primitive, gets no
replacement path.

### 9.4 New TLV type (proposed)

| Type | Value | Meaning |
|---|---|---|
| `EOS_TLV_TARGET` | `0x41` | Target identity: board identity + stage marker (`"stage-1"`). Required on bootloader-replacement images; ignored (not required) on application images. |

Value `0x41` is the next free value after `EOS_TLV_DEPENDENCY` (`0x40`);
existing types (`0x01`, `0x10`, `0x24`, `0x40`, `0x50`, `0x51`, `0x60`)
are unchanged. The exact encoding of the board identity (string vs.
numeric board id) is an implementation decision; the note requires only
that it be inside the TLV area bound by `tlv_hash` and that it distinguish
boards sharing an address map.

## 10. Transaction walk-through (normative sequence)

1. **Stage.** Application / stage-1 streams the candidate stage-1 image
   into `EOS_SLOT_RECOVERY` via the existing update path. Nothing in the
   bootloader region is touched.
2. **Verify (stage-1).** Stage-1 runs §3 steps 1–5 against the staged image.
   Failure: abort, log, leave the old stage-1 alone.
3. **Record intent.** Stage-1 writes the intent record (`PENDING`, with
   expected digest / counter / size) into bootctl and resets.
4. **Re-verify (stage-0).** Stage-0 loads bootctl, sees `PENDING`, and
   re-runs §3 steps 1–5 from the staged image. Failure: clear the record,
   log, boot the old stage-1.
5. **Lift WRP.** Stage-0 calls the proposed `flash_unprotect_bootloader()`,
   sets the record to `COPYING`, and resets (STM32F4: option bytes reload
   on reset). Until this reset completes, the bootloader region is still
   hardware-protected; the window in which it is unprotected lasts exactly
   one boot cycle, runs no application code, and is bounded by the watchdog.
6. **Copy.** Stage-0 sets the one-shot RAM authorization token, erases the
   stage-1 sectors, copies the staged image through the privileged HAL
   write, and clears the token.
7. **Verify written bytes.** Stage-0 re-hashes the stage-1 region and
   compares it against the staged image's digest (and the record's expected
   digest). Mismatch: halt; the next boot enters recovery (§6).
8. **Re-apply WRP.** Stage-0 calls `eos_bootloader_protect_apply()` and
   verifies with `eos_bootloader_protect_verify()`. Failure: halt (§5).
9. **Clear + reset.** Stage-0 clears the intent record and resets.
10. **Boot + commit.** Stage-0 verifies the new stage-1 by signature (the
    §3 prerequisite) and jumps. The new stage-1 completes a full boot
    decision, then commits the staged counter (§4).

## 11. Open questions for reviewers

1. **Stage-0 size budget.** Stage-0 re-verification (§3) needs Ed25519 +
   keystore + TLV + rollback code in the 16 KiB stage-0. The code is already
   in `eboot_core`, which stage-0 links — but the *size* impact is unmeasured.
   If it does not fit, what gives: a slimmer verify path, a larger stage-0
   sector, or a different split? Measure before implementing.
2. **Staging slot.** The note stages in `EOS_SLOT_RECOVERY` (128 KiB on
   STM32F4, comfortably holds a 48 KiB stage-1). That overwrites any
   recovery image staged there. Is that acceptable, or should staging use
   the inactive application slot (destroying a confirmed-good app image
   instead)? The note prefers the recovery slot because a lost recovery
   image is recoverable over UART while a lost confirmed-good app image is
   not — confirm or overturn.
3. **Counter-bump discipline.** §4 says bump `MIN_SEC_VER` only on security
   events / key rotation. Should a stage-1 release that changes the boot
   policy (not a vulnerability) also bump, on the grounds that rolling back
   the policy is itself a security regression? The note says no; reviewers
   may say yes.
4. **Halt vs. recovery on WRP re-apply failure.** §5 halts after a good copy
   if WRP cannot be re-applied, consistent with #191. An alternative is to
   enter recovery immediately instead of halting, so the device can be
   re-staged without a probe. But recovery itself needs the region writable
   to fix anything — is "halt and wait for a probe" or "recover with WRP
   down" the right fail-closed posture here?
5. **Target-identity encoding.** §9.4 leaves the `EOS_TLV_TARGET` value
   encoding open (string vs. numeric board id). It must distinguish boards
   sharing an address map; pick the encoding at implementation time and pin
   it in the TLV documentation.
6. **Should the replacement be operator-confirmable?** The transaction as
   designed is automatic once requested. For a fleet, is an explicit
   operator confirmation (a second, distinct step between staging and
   intent) worth the added state machine, or does the signature + counter
   gate already carry the authorization?

## 12. Amendment — privileged range narrowed to stage-1 (2026-11-23)

As merged in #200, the token-gated privileged HAL entries
(`eos_hal_flash_write_bootloader()` / `eos_hal_flash_erase_bootloader()`,
`hal/hal_core.c`) admitted the whole board-declared bootloader region
(stage-0 + stage-1), while §1 of this note defines "bootloader region" as
the stage-1 part. The follow-up narrows the gate to
`[stage1_base, bootloader_end)`, so the implementation now matches §1: a
privileged write to stage-0 is refused with `EOS_ERR_INVALID`, exactly like
any other out-of-range privileged call.

The stage-1 base is derived, not declared: `stage1_base = flash_base +
app_vector_offset` — the same formula the stage-0 executor
(`stage0/bootloader_replace.c`), the request-side verify
(`core/bootloader_replace.c` §3 step 2), and the stage-0 jump
(`stage0/jump_stage1.c`) already use for the stage-1 vector address. No new
board field was added, so there is no second source of truth that could
disagree with the jump target.

Fail-closed rule: a board whose derived stage-1 base does not lie strictly
inside its declared bootloader region declares no usable stage-1 boundary,
and the privileged entries refuse every range rather than falling back to
the whole region. That covers `app_vector_offset == 0` (stage-1 at the flash
base: no stage-0/stage-1 split declared), an offset landing at or below the
region start, one landing at or past the region end, and an offset that wraps
`flash_base` past 2^32. STM32F4 is the only board that declares a bootloader
region; every other board already refused privileged ops for want of one.

The §3 prerequisite is unchanged by this amendment and still open:
signature-based stage-1 verification in stage-0 is NOT implemented here, and
fielded hash-pinned stage-0 devices still cannot use in-band replacement.
That remains the field-deployment gate, tracked as a separate change.

## 13. References

- eBoot #191 — bootloader self-protection (HAL allow-list + STM32F4 WRP);
  `include/eos_bootloader_protect.h`, `core/bootloader_protect.c`,
  `stage0/hw_init_minimal.c`.
- eBoot #177 — anti-rollback fail-closed rule; #181 — OTP-backed monotonic
  counter; #189 — keystore revoke; #188 — debug lock.
- `docs/adr/ADR-020-install-path-verifies-signature-before-anti-rollback.md`
  — signature before anti-rollback.
- `docs/secure_boot_chain.md` §2.2 — Phase-2 stage-0 verification flow
  (signature-based; the prerequisite in §3).
- `docs/key_lifecycle.md` §6 — key revocation via monotonic counter;
  §6.3 counter management rules.
- `docs/mcuboot_comparison.md` §3.1 (swap rejected), §4.3 (stage-0 rarely
  updated, stage-1 more frequently).
- `docs/update-flow.md` — OTA update sequence and rollback triggers.
- `include/eos_image.h` — 156-byte header v2, 92-byte signed prefix,
  `EOS_TLV_MIN_SEC_VER`; `include/eos_rollback.h`, `include/eos_keystore.h`,
  `include/eos_bootctl.h`, `include/eos_recovery.h`.
- Zephyr / MCUboot + mcumgr/SMP image management (in-band DFU reference
  pattern; see §7 for the ADAPT statement and its limits).

---

*Implementation landed in PR #200 (merged 2026-11-23); the §3 prerequisite
— signature-based stage-1 verification in stage-0 — remains open as the
field-deployment gate, and fielded hash-pinned stage-0 devices cannot use
in-band replacement until it lands. The privileged range was narrowed to
stage-1 in the 2026-11-23 follow-up (§12).*

— av_mando
