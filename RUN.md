# eBoot — TLV anti-rollback fix: build, test, and reproduce

Everything below runs on a plain Linux host (or macOS). No board, no
cross-compiler, no hardware needed — the native build compiles the
platform-agnostic core libraries and the whole unit-test suite.

The finding and the reasoning are in `PROPOSAL.md`.

---

## 0. Prerequisites

```bash
# Debian / Ubuntu
sudo apt update
sudo apt install -y build-essential cmake git python3 python3-pip valgrind

# macOS (Homebrew)
brew install cmake python git
```

Python bits used by the script-driven suites:

```bash
pip3 install -r requirements.txt
pip3 install pytest
```

> On newer Debian/Ubuntu, `pip3` may refuse to touch system packages. Either
> use a virtualenv, or append `--break-system-packages` to the two commands.

`valgrind` is optional. Without it, CMake skips the 17 valgrind targets and
`ctest` runs 21 tests instead of 38; everything else is identical.

---

## 1. Build

```bash
cd eBoot
cmake -B build -DEBLDR_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

One `#warning` during the build is expected and intentional — it fires because
no production signing key is configured, so the RFC 8032 test-vector key is
being compiled in as the trust anchor.

---

## 2. Run the tests

```bash
ctest --test-dir build --output-on-failure
```

Expected: **`100% tests passed, 0 tests failed out of 38`** (21 without
valgrind). The new one is `test_tlv_auth`, which you can run on its own:

```bash
./build/tests/eboot_test_tlv_auth
```

```
TLV authentication (anti-rollback counter)

  test_authenticated_tlv_counter_is_read                     [PASS]
  test_tampered_tlv_counter_is_rejected                      [PASS]
  test_tamper_is_invisible_to_signature_and_integrity        [PASS]
  test_unbound_tlv_area_is_not_trusted                       [PASS]
  test_image_without_tlv_area_still_reports_zero             [PASS]
  test_tlv_binding_fields_are_inside_the_signed_prefix       [PASS]
  test_oversized_tlv_len_is_rejected                         [PASS]

7/7 passed
```

The script-driven suites:

```bash
python3 run_all_tests.py                  # expect 33 passed
python3 tests/production_test_suite.py    # expect 1 failure: SA-6.9 (pre-existing)
```

`SA-6.9` wants a sanitizer job in the CI workflow. It is unrelated to this
change and deliberately left alone.

---

## 3. Reproduce the original vulnerability

This is the part worth doing yourself. It builds **upstream, unpatched** eBoot
in a throwaway worktree and runs the proof of concept against it.

```bash
export EBOOT="$PWD"          # run this from the root of this archive
git worktree add /tmp/eboot-upstream 13a7a02
cd /tmp/eboot-upstream
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
gcc -I include "$EBOOT/poc/tlv_downgrade_poc.c" -o /tmp/poc \
    build/libeboot_core.a build/libeboot_hal.a -lm
/tmp/poc
```

**Before the fix — the downgrade succeeds:**

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

Four bytes were rewritten. The SHA-256 payload check still passes and not one
byte of the signed header prefix changed — because the TLV area sits outside
both.

**After the fix — same binary, patched library:**

```bash
cd "$EBOOT"
gcc -I include poc/tlv_downgrade_poc.c -o /tmp/poc_after \
    build/libeboot_core.a build/libeboot_hal.a -lm
/tmp/poc_after
```

```
read_image_counter (untampered) : rc=0 counter=0
rollback_verify(0)              : -14  (rejected, as it should be)
...
  read_image_counter            : rc=0 counter=0
  rollback_verify(0)            : -14  (rejected)
```

The image declares no authenticated TLV area, so nothing in it can raise the
anti-rollback floor.

Clean up the worktree when done:

```bash
git worktree remove --force /tmp/eboot-upstream
```

---

## 4. Read the change

```bash
git log --oneline 13a7a02..HEAD
git diff 13a7a02 -- core/rollback.c include/eos_image.h
git show --stat HEAD~1
```

The substance is in three files:

| File | What changed |
|---|---|
| `include/eos_image.h` | `reserved[30]` → `tlv_len` (2 B) + `tlv_hash` (28 B), both inside the signed prefix, plus static asserts pinning the offsets |
| `core/rollback.c` | hash the declared TLV area and refuse to read a counter from one that does not match |
| `tests/unit/test_tlv_auth.c` | the regression test, 7 cases |

---

## 5. Optional: cross-compile check

Not required, and not something I validated. If you have the toolchain:

```bash
cmake -B build-arm -DEBLDR_BOARD=stm32f4 \
  -DCMAKE_TOOLCHAIN_FILE=toolchains/arm-none-eabi.cmake
cmake --build build-arm --parallel
```
