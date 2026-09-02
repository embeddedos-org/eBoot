# Proof of concept — unauthenticated anti-rollback counter

`tlv_downgrade_poc.c` demonstrates, against **unmodified** eBoot at `13a7a02`,
that the security counter gating `eos_rollback_verify()` can be raised by
rewriting four bytes that neither the image signature nor the payload hash
covers.

It is a host program, not a test: it links `libeboot_core` and a simulated
flash, lays down a well-formed image declaring counter 3, sets the device
anti-rollback floor to 9, and then rewrites the `EOS_TLV_MIN_SEC_VER` value in
the TLV area that sits after the payload.

## Run it against upstream

```bash
git checkout 13a7a02
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel
gcc -I include poc/tlv_downgrade_poc.c -o /tmp/poc \
    build/libeboot_core.a build/libeboot_hal.a -lm
/tmp/poc
```

Expected output before the fix:

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

After the fix on this branch the same binary reports `counter=0` in both
cases and `rollback_verify` returns `-14` (`EOS_ERR_ANTI_ROLLBACK`) both
times: the image no longer declares an authenticated TLV area, so nothing in
it can raise the floor.

The behaviour is pinned as a real regression test in
`tests/unit/test_tlv_auth.c`; this directory exists so the original finding
can be reproduced against upstream without applying the patch.
