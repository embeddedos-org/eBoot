# Development

## Contribution source of truth

[CONTRIBUTING](https://github.com/embeddedos-org/eBoot/blob/master/CONTRIBUTING.md)

Before proposing a change, also review the [README](https://github.com/embeddedos-org/eBoot/blob/master/README.md). Keep changes scoped, add tests appropriate to the affected behavior, and follow the repository's current automation and review requirements.

## Build and dependency inputs found

`CMakeLists.txt`, `Dockerfile`, `build_sim/Makefile`, `requirements.txt`, `tests/CMakeLists.txt`, `tests/fuzz/CMakeLists.txt`.

## Tests found in the default-branch tree

`ci/security_test.sh`, `tests/CMakeLists.txt`, `tests/__init__.py`, `tests/functional/__init__.py`, `tests/functional/test_functional_e2e.py`, `tests/fuzz/CMakeLists.txt`, `tests/fuzz/fuzz_bootctl.c`, `tests/fuzz/fuzz_crypto.c`, `tests/fuzz/fuzz_fdt.c`, `tests/fuzz/fuzz_fw_update.c`, `tests/fuzz/fuzz_image_verify.c`, `tests/fuzz/fuzz_recovery_protocol.c`, and 59 more.

## Documented test commands

These commands are reproduced from the inspected root README or contributing guide:

```bash
cmake -S . -B build \
```

```bash
cmake --build build --parallel
```

```bash
ctest --test-dir build --output-on-failure
```

```bash
cmake -B build/stm32 \
```

```bash
cmake --build build/stm32 --parallel
```

```bash
cmake -S . -B build -DEBLDR_BUILD_TESTS=ON
```

```bash
cmake -B build -DEBLDR_BUILD_TESTS=ON
```

```bash
cmake --build build
```

## Verification baseline

This inventory comes from `master` at [`b42935469821`](https://github.com/embeddedos-org/eBoot/commit/b42935469821343b972738690e3d3a925c223edf) and found 71 test-related paths among 540 files. Re-check the source tree when that commit is no longer current.
