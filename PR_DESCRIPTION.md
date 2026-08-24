# Fix Storage Bounds Checks and Integer Overflow

Fixes unsafe storage boundary validation in `eos_storage_read()`, `eos_storage_write()`, and `eos_storage_erase()`.

The existing read/write checks used `off + len`, which could overflow a 32-bit unsigned integer and bypass the bounds check. `eos_storage_erase()` did not perform a device-size boundary check at all.

The fix uses overflow-safe validation based on subtraction and adds regression coverage for out-of-bounds and overflow cases.

## Issue

The unified storage abstraction in [`core/storage.c`](core/storage.c) has two
related boundary-validation defects:

1. **`eos_storage_erase()` has no bounds check.** Any `(offset, length)` pair is
   forwarded directly to the underlying driver callback without validating
   against `dev->total_size`. Out-of-bounds erases silently succeed.

2. **`eos_storage_read()` and `eos_storage_write()` use an overflow-unsafe
   bounds check.** The expression `off + len > dev->total_size` wraps around
   when `off + len` exceeds `UINT32_MAX`. For example, `off = 0xFFFFFFFF` and
   `len = 10` produces `off + len = 9`, which passes the check on any device
   with `total_size >= 10`, allowing out-of-bounds driver operations.

## Root Cause

```c
// eos_storage_read / eos_storage_write — before fix:
if (off + len > dev->total_size) return -1;  // wraps on uint32 overflow

// eos_storage_erase — before fix:
// (no bounds check at all)
return dev->ops->erase(dev->ctx, off, len);
```

## Fix

Replace the naive addition with the overflow-safe equivalent in all three
functions:

```c
if (off > dev->total_size || len > dev->total_size - off) return -1;
```

The subtraction `dev->total_size - off` is only evaluated after confirming
`off <= dev->total_size`, so it cannot underflow. This pattern is safe for all
`uint32_t` values.

### Production diff (3 lines changed, 0 lines removed)

```diff
 int eos_storage_read(...)
 {
     if (!dev || !dev->initialized || !dev->ops->read) return -1;
-    if (off + len > dev->total_size) return -1;
+    if (off > dev->total_size || len > dev->total_size - off) return -1;

 int eos_storage_write(...)
 {
     ...
-    if (off + len > dev->total_size) return -1;
+    if (off > dev->total_size || len > dev->total_size - off) return -1;

 int eos_storage_erase(...)
 {
     ...
+    if (off > dev->total_size || len > dev->total_size - off) return -1;
```

## Reproduction

A regression test with a 64 KB mock storage backend demonstrates both defects
against the original code:

| Test case | Original result | Fixed result |
|-----------|----------------|--------------|
| `eos_storage_erase(dev, 60KB, 10KB)` — extends past 64 KB | `0` (success, driver invoked) | `-1` (rejected) |
| `eos_storage_erase(dev, 0xFFFFFFFF, 10)` — uint32 wrap | `0` (success, driver invoked) | `-1` (rejected) |
| `eos_storage_read(dev, 0xFFFFFFFF, buf, 10)` — uint32 wrap | `0` (success, driver invoked) | `-1` (rejected) |
| `eos_storage_write(dev, 0xFFFFFFFF, buf, 10)` — uint32 wrap | `0` (success, driver invoked) | `-1` (rejected) |

## Testing

**Regression tests** ([`tests/unit/test_storage.c`](tests/unit/test_storage.c)):
- `test_erase_out_of_bounds` — erase past device end
- `test_erase_integer_overflow` — erase with uint32 wrap-around offset
- `test_read_integer_overflow` — read with uint32 wrap-around offset
- `test_write_integer_overflow` — write with uint32 wrap-around offset
- `test_valid_read_write_erase` — normal in-bounds and exact-boundary ops
- `test_write_protect_blocks_write_and_erase` — write-protect still enforced

**Full suite results:**
- `test_storage`: **6/6 passed**
- CTest (all targets): **12/12 passed (100%)**
- pytest: **5/5 passed (100%)**

## Files Changed

| File | Change |
|------|--------|
| `core/storage.c` | +3 lines changed (overflow-safe bounds checks) |
| `tests/CMakeLists.txt` | +4 lines (register test_storage target) |
| `tests/unit/test_storage.c` | New file — regression and functional tests |
