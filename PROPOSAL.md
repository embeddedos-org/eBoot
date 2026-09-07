# eBoot — Technical Assessment Submission

**N Vignesh Reddy** · nvigneshreddy26@gmail.com · github.com/vignesh917
**Project reviewed:** eBoot (`embeddedos-org/eBoot`), commit `13a7a02`
**Finding:** the anti-rollback security counter is read from unauthenticated flash

---

## 1. The issue

eBoot's anti-rollback gate reads the image's security counter from the
`EOS_TLV_MIN_SEC_VER` entry in the image's TLV area
(`core/rollback.c::eos_rollback_read_image_counter`), and `eos_secure_boot()`
uses that value in step 5b to decide whether an image is new enough to boot.

The TLV area is not covered by anything the bootloader verifies:

| Check | Region it covers |
|---|---|
| `eos_image_verify_signature()` | `header[0 .. EOS_IMG_SIGNED_LEN)` = bytes 0–91 |
| `eos_image_verify_integrity()` | `[hdr_size, hdr_size + image_size)` — the payload |
| TLV area | starts at `hdr_size + image_size` — **after** both |

So the one input that decides whether a downgrade is allowed sits in the only
part of the image nothing signs or hashes.

An attacker who can write flash — physical access, an exposed debug port, or a
compromised recovery agent, since `recovery_handle_write()` accepts any offset
inside the slot — can take a **genuinely signed old image**, rewrite four bytes
of TLV so its declared counter clears the device floor, and boot it. That is
exactly the downgrade anti-rollback exists to stop. This is not a bypass of
signature checking: the image is authentic, it is just old, and the mechanism
meant to reject it can be told otherwise.

A second consequence: `eos_secure_boot()` calls `eos_rollback_stage(img_counter)`
with the attacker's value, and `eos_bootctl_confirm()` later commits it to the
monotonic counter. `EOS_ROLLBACK_MAX_STEP` caps a single advance at 16, but a
repeatable +16 per confirmed boot is a path to burning the fuse counter past
every legitimate image — a permanent brick.

I also found the codebase disagreed with itself about where the TLV area is.
`include/eos_image_tlv.h` documented `[image_header][TLV area][payload]` and
claimed "TLVs are covered by the image signature for tamper protection";
`core/image_verify.c` and `core/rollback.c` both implement
`[image_header][payload][TLV area]`, and the signature covers no part of it.
`tools/eos_sign.py` follows the header's layout and carries a `KNOWN LIMITATION`
note saying the images it produces do not boot as a result.

### Demonstration against unmodified `13a7a02`

A short host program against `libeboot_core` (`poc/tlv_downgrade_poc.c`), with
the device floor at 9 and a signed image declaring counter 3:

```
device anti-rollback floor      : 9
read_image_counter (untampered) : rc=0 counter=3
rollback_verify(3)              : -14  (rejected, as it should be)

after rewriting 4 TLV bytes:
  signed header prefix changed? : NO
  parse_header                  : 0
  verify_integrity (SHA-256)    : 0  (still PASSES)
  read_image_counter            : rc=0 counter=9
  rollback_verify(9)            : 0  (ACCEPTED -- downgrade succeeded)
```

Same binary against the patched tree: the counter reads 0 both before and
after, and `rollback_verify` returns `EOS_ERR_ANTI_ROLLBACK` in both cases.

---

## 2. Proposed solution

Bind the TLV area to the signed image header, the way mcuboot's *protected*
TLVs are covered by the signature, rather than trusting bytes that sit outside
every check.

The header has 30 reserved bytes at offsets 62–91, and — usefully —
`EOS_IMG_SIGNED_LEN` is 92, so those bytes are already inside the signed
prefix. I split them into:

```c
uint16_t tlv_len;                              /* offset 62, 2 bytes  */
uint8_t  tlv_hash[EOS_IMG_TLV_HASH_LEN];       /* offset 64, 28 bytes */
```

`tlv_hash` is SHA-256 of the TLV area truncated to 224 bits. Every other field
keeps the byte offset the signing tools address, the header stays 156 bytes on
the wire, and the static asserts in `eos_image.h` (plus `test_image_abi.c`) pin
that.

`eos_rollback_read_image_counter()` then:

- `tlv_len == 0` → the image makes no authenticated claim about a TLV area, so
  the area is not read and the counter reports **0**;
- `tlv_len` outside `[sizeof(eos_tlv_info_t), EOS_TLV_MAX_SIZE]`, or the address
  range wrapping → `EOS_ERR_INVALID`;
- otherwise, hash `tlv_len` bytes at `hdr_size + image_size` and compare against
  `tlv_hash`. Mismatch → `EOS_ERR_INVALID`; match → parse and read the counter
  as before.

---

## 3. Technical approach and why these choices

**Why 224 bits and not a full 32-byte digest.** A full digest plus a length
needs 36 bytes and there are 30. Widening the header would move `signature[]`
off offset 92 and break the on-disk format that `sign_image.py`, `imgpack.py`,
`eos_sign.py` and `test_image_abi.c` all address by absolute offset. Truncated
SHA-256 is standard practice (SHA-256/224 is a FIPS 180-4 variant), and what
matters here is second-preimage resistance on a ~64-byte blob — 224 bits is far
past anything reachable. Trading 32 bits of margin for a format that does not
move felt like the right side of that trade; I have noted the alternative in
§5.

**Why absence reads as 0 rather than failing.** A counter of 0 can only fail
against the device floor, never clear it. So "no authenticated TLV area" is
already the conservative answer and needs no special case. This is also what
keeps the change backward compatible: every image the project's tools currently
produce has those bytes zeroed, so `tlv_len` reads 0 and behaviour is identical
to today. `tools/sign_image.py` — the one the CI path uses — emits
`[header][payload]` with no TLV area at all, so nothing regresses there.

**Why a claimed-but-mismatched area fails closed instead of falling back to 0.**
Falling back would let tampering degrade the check silently. If the header
vouches for an area and the bytes disagree, something rewrote flash; that
should be reported, and `eos_secure_boot()` already turns a non-`EOS_OK` return
from this function into a refusal to boot.

**Why the digest is streamed.** The area is hashed in 64-byte chunks through
`eos_sha256_update()` rather than buffered, so stack use is fixed (~64 B + the
SHA-256 context) instead of scaling with `EOS_TLV_MAX_SIZE`. Stage-1 stacks on
the smaller targets in `boards/` are tight.

**Why the comparison is constant-time.** It runs over attacker-influenced
bytes, and a bootloader gives an attacker unlimited retries with a logic
analyser. It matches the `secure_compare()` pattern already used in
`keystore.c` and `secure_boot.c`.

**Documentation.** `eos_image_tlv.h` now states the layout the code actually
implements, and says plainly that the area is covered by neither the signature
nor `hash[]` and must be bound through `tlv_len`/`tlv_hash` to be trusted.

---

## 4. Testing and validation

**New regression test** — `tests/unit/test_tlv_auth.c`, registered in
`tests/CMakeLists.txt` for both ctest and the valgrind sweep. Seven cases:

| Test | What it pins |
|---|---|
| `authenticated_tlv_counter_is_read` | a correctly bound area still reads normally |
| `tampered_tlv_counter_is_rejected` | the downgrade now returns `EOS_ERR_INVALID` |
| `tamper_is_invisible_to_signature_and_integrity` | the tampered image still passes SHA-256 and leaves the signed prefix byte-identical — proving the binding is the only thing that can catch it |
| `unbound_tlv_area_is_not_trusted` | an area the header does not vouch for reports 0 |
| `image_without_tlv_area_still_reports_zero` | no behaviour change for `sign_image.py` images |
| `tlv_binding_fields_are_inside_the_signed_prefix` | `tlv_len`/`tlv_hash` stay under `EOS_IMG_SIGNED_LEN` |
| `oversized_tlv_len_is_rejected` | a bogus `tlv_len` is refused before any hashing |

The third case is the one I would point a reviewer at: it fails only if someone
later moves the TLV area back under an existing check, and it documents why the
new field exists.

**Results** (Linux x86-64, GCC, `-DEBLDR_BUILD_TESTS=ON`, Debug):

- `ctest` — **38/38 passed**, including 17 valgrind targets (`--leak-check=full
  --error-exitcode=1`), up from 36 before.
- `python run_all_tests.py` — **33/33 passed**.
- `python tests/production_test_suite.py` — 1 failure remaining, `SA-6.9: CI
  workflow has sanitizer job`, pre-existing and out of scope here.

**Not covered by my testing, and I want to be clear about it:** I only built
and ran the native host configuration. I did not cross-compile for any board in
`boards/`, and I have no hardware, so nothing here is validated on a real
target.

### Two small things fixed on the way

Both were in the way of running the suite that proves the main change:

- `tests/production_test_suite.py` hard-coded `/home/ubuntu/eBoot`, so it
  crashed with a `FileNotFoundError` for anyone else. Now derived from
  `__file__`. Running it revealed two pre-existing failures that the crash had
  been hiding — the SPDX one below, and `SA-6.9`.
- `core/sha512.c` was missing its SPDX header (`SA-6.6`). Added.

---

## 5. Limitations and things I would want a maintainer's call on

1. **The signing tools are not updated to emit the binding.** Nothing regresses
   — every current tool writes zeros into those bytes, which now means "no
   authenticated TLV area" — but until `sign_image.py` learns to append a TLV
   area and fill `tlv_len`/`tlv_hash`, a TLV-declared security counter is
   effectively unusable rather than merely untrusted. That is a deliberate
   fail-closed default, not a finished feature. I left it out because the
   format question in §5.2 should be settled first.

2. **This is a header-format decision, not just a bug fix.** Consuming the
   reserved bytes is cheap and offset-stable, but it spends the header's only
   remaining space and settles on a truncated digest. The alternatives —
   bumping `hdr_version` to 3 and widening the header, or moving the TLV area
   in front of the payload so `hash[]` covers it (which is what
   `eos_image_tlv.h` used to claim and `eos_sign.py` still assumes) — are both
   defensible and both break existing images. I picked the option that changes
   nothing for images already in the field; a maintainer may reasonably prefer
   one of the others.

3. **The `eos_sign.py` / `sign_image.py` split is still unresolved.**
   `eos_sign.py` emits `[header][TLV][payload]`, which no part of the
   bootloader reads. My change makes the intended layout explicit in the
   headers, but does not reconcile the two tools.

4. **Threat model.** This matters only against an attacker who can write flash.
   That is deliberately the anti-rollback threat model — an attacker who cannot
   write flash has no way to install the old image in the first place — but it
   is worth stating that this is not a remote-network finding.

5. **The TLV read is still not bounded against slot capacity.** `verify_slot()`
   bounds `hdr_size + image_size` against `eos_hal_slot_size()`, but nothing
   bounds the TLV area that follows, so hashing `tlv_len` bytes can read up to
   512 bytes past the end of a slot that an image exactly fills. This is not a
   regression — `eos_tlv_parse()` already read the same region, bounded the
   same way — and every HAL `flash_read` I looked at rejects an out-of-range
   address. It is still a loose end, and the natural place to close it is the
   slot-capacity check in `slot_manager.c`, alongside the one already there.

6. **Not addressed, though I noticed them.** `eos_secure_boot()` step 4 reads
   the OTP root-of-trust key hash and then does nothing with it (two `TODO`-ish
   comments where the TLV keyhash comparison should be). `eos_storage_dump()`
   is the only function in `storage.c` that does not null-check `dev`. Both
   looked like separate changes rather than things to fold into this one.

---

## Files changed

```
 core/rollback.c                |  69 +++++++++    the fix
 include/eos_image.h            |  34 ++++-       tlv_len / tlv_hash + static asserts
 include/eos_image_tlv.h        |  16 ++-         corrected layout documentation
 tests/unit/test_tlv_auth.c     | 309 +++++++++   new regression test
 tests/CMakeLists.txt           |   8 +-          register it (ctest + valgrind)
 tests/unit/test_image_abi.c    |   3 +-          ABI pins for the new fields
 tests/unit/test_image_verify.c |   3 +-          signed-prefix coverage assertions
 tests/unit/test_slot_manager.c |   8 +-          mock used reserved[0]
 tests/production_test_suite.py |   6 +-          hard-coded path
 core/sha512.c                  |   3 +           missing SPDX header
 tools/eos_sign.py              |   2 +-          comment: name the new fields
 tools/imgpack.py               |   2 +-          same
```

Per the screening instructions I have not opened a pull request against
`embeddedos-org/eBoot`. The branch and the proof-of-concept are on my fork and
I am happy to open one if the team would like it.

I used AI assistance while working through this, and I have read and can walk
through every line of it — including why 224 bits, why absence reads as zero,
and why the mismatch case fails closed rather than degrading.
