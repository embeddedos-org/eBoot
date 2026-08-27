<!-- generated: eos-ai-scaffold -->
# Tasks

Working ledger for `eBoot`. The planner writes entries; each owning role
updates its own row. Roles are in [AGENTS.md](./AGENTS.md), the workflow in
[ORCHESTRATION.md](./ORCHESTRATION.md), the gate in [VERIFY.md](./VERIFY.md).

Status is one of: `todo`, `in-progress`, `blocked`, `review`, `done`.

## Active

| ID | Task | Owner | Mode | Status | Depends on |
|----|------|-------|------|--------|------------|
| —  | No active tasks. | — | — | — | — |

## Completed

| ID | Task | Owner | Verified by | Evidence |
|----|------|-------|-------------|----------|
| T-001 | Make Ed25519 verification actually work (it rejected every valid signature) | security | reviewer | `eos_ed25519_verify()` rejected all three RFC 8032 §7.1 vectors and rejected a signature made by `python-cryptography` — the same library `tools/sign_image.py` signs with — so **no image signed by this repo's own tool could ever boot**. Two independent causes: (a) the challenge used SHA-256 zero-padded to 64 bytes instead of SHA-512, and (b) the curve arithmetic was wrong at the base case — `[1]B` did not return the base point, and `ge_frombytes()` failed to decode valid RFC 8032 points. Replaced `core/ed25519_verify.c` with a correct RFC 8032 verifier and added `core/sha512.c`. Now: 3/3 RFC vectors accepted; 512/512 single-bit signature flips rejected; non-canonical S (S+L) rejected. |
| T-002 | Add SHA-512 (FIPS 180-4), which Ed25519 requires | backend | reviewer | New `core/sha512.c`. Digests for empty, `abc`, 1,000,000×`a`, and a 128-byte block match `sha512sum` exactly; streaming digests match the one-shot result across chunk sizes 1/63/111/112/113/127/128/129/255. |
| T-003 | Replace Ed25519 tests that could not fail | testing | reviewer | Every test in `tests/unit/test_ed25519.c` asserted only that bad signatures are *rejected*, which a verifier that rejects everything passes trivially — that is precisely the defect it missed. Rewritten to assert both directions, RFC 8032 vectors first. 10/10 pass; the positive-vector test fails against the old implementation. |
| T-004 | CI ran zero tests and reported success | testing | reviewer | `ci.yml` configured with `-DBUILD_TESTS=ON`, but this project's option is `EBLDR_BUILD_TESTS`; the flag set an unrelated cache variable and no test was ever built. `ctest` exits 0 when it finds no tests (verified: exit code 0), so the job passed green. This is how T-001 shipped. Fixed the flag and added `--no-tests=error`; with the old flag the job now exits 8. CI runs 11 tests. |
| T-005 | Fix a mismatched `extern` that no compiler could see | backend | reviewer | `core/secure_boot.c` declared `eos_ed25519_verify(msg, msg_len, sig, pubkey)` while the definition is `(signature, public_key, message, msg_len)` — a caller would have passed a `size_t` length where a key pointer was expected. Both ad-hoc `extern`s removed; the single prototype now lives in `include/eos_crypto_boot.h`. |
| T-006 | Build the two source files that were never compiled | backend | reviewer | `core/secure_boot.c` and `core/fdt_loader.c` were absent from `CMakeLists.txt`. `secure_boot.c` also called `eos_sha256()`, which was defined nowhere in the tree, so it could not have linked. Added the one-shot `eos_sha256()` to `core/crypto_boot.c` and both files to the build; clean under `-Wall -Wextra`. |

---

## Task template

```markdown
### T-000 — <short title>

Owner: <role>
Mode: <see MODES.md>
Status: todo
Depends on: <task ids, or none>

Goal
: <one sentence: what is true afterwards that is not true now>

Acceptance criteria
: - <observable, checkable statement>
  - <observable, checkable statement>

Files in scope
: <paths the owner is expected to touch>

Out of scope
: <what this task deliberately does not change>

Risks
: <what could break, and what would reveal it>

Verification
: | Check | Command | Result |
  |-------|---------|--------|
  | <name> | `<command>` | `NOT RUN` |
```

## Verification commands for this repository

These commands were derived from the manifests at the repository root. Confirm one works before relying on it; a listed script may still be a stub.

| Check | Command | Default state |
|-------|---------|---------------|
| Unit tests | `pytest` | `NOT RUN` |
| Build | `cmake --build build -j` | `NOT RUN` |

## Rules

- One task per unit of work that can be verified on its own.
- Acceptance criteria are written before work starts and are not edited to match
  what was built. If they were wrong, say so and rewrite them explicitly.
- A task reaches `done` only when the definition of done in
  [ORCHESTRATION.md](./ORCHESTRATION.md) is met and the verification commands
  were actually run.
- `blocked` requires a note naming what it is blocked on and who can unblock it.
