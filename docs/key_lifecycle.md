# Key Lifecycle Management — eBootloader

> **Last updated:** 2026-04-03
>
> **Applies to:** Ed25519 signing keys used for firmware image verification in the eBootloader secure boot chain.

---

## 1. Overview

eBootloader uses **Ed25519** (RFC 8032) digital signatures to verify firmware image authenticity and integrity. This document defines the full lifecycle of the signing keypair — from generation through rotation, revocation, and emergency response.

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│  Generate    │────>│   Deploy     │────>│   Verify     │
│  Keypair     │     │   Public Key │     │   at Boot    │
└──────────────┘     └──────────────┘     └──────────────┘
       │                    │                     │
       │              ┌─────┴─────┐               │
       │              │ Key Slot 0│               │
       │              │ Key Slot 1│               │
       │              └───────────┘               │
       │                                          │
       ▼                                          ▼
┌──────────────┐                          ┌──────────────┐
│   Rotate     │                          │   Revoke     │
│   (new key)  │                          │   (counter)  │
└──────────────┘                          └──────────────┘
```

---

## 2. Ed25519 Keypair Generation

### 2.1 Algorithm Specification

| Parameter | Value |
|---|---|
| Algorithm | Ed25519 (EdDSA over Curve25519) |
| Private key size | 32 bytes |
| Public key size | 32 bytes |
| Signature size | 64 bytes |
| Hash function | SHA-512 (internal to Ed25519) |
| Reference | RFC 8032, Section 5.1 |

### 2.2 Generation Procedure

**Prerequisites:**
- Air-gapped workstation running a hardened OS (no network interfaces active)
- Hardware random number generator (TRNG) or `/dev/random` with sufficient entropy
- OpenSSL ≥ 1.1.1 or `ed25519-keygen` from the eBootloader tools

**Steps:**

```bash
# 1. Generate Ed25519 private key (PEM format)
openssl genpkey -algorithm Ed25519 -out eboot_signing_key.pem

# 2. Extract public key
openssl pkey -in eboot_signing_key.pem -pubout -out eboot_signing_pub.pem

# 3. Export raw 32-byte public key for embedding
openssl pkey -in eboot_signing_key.pem -pubout -outform DER | \
    tail -c 32 > eboot_signing_pub.raw

# 4. Generate SHA-256 hash of public key (for TLV embedding)
sha256sum eboot_signing_pub.raw > eboot_signing_pub.sha256

# 5. Verify the keypair
echo "test" | openssl pkeyutl -sign -inkey eboot_signing_key.pem | \
    openssl pkeyutl -verify -pubin -inkey eboot_signing_pub.pem
```

**Alternative — using eBootloader tooling:**

```bash
# Generate the keypair and the value the bootloader build takes as its
# trust anchor. Writes keys/private.pem, keys/public.pem and
# keys/public_key.hex (64 hex characters). There is no header to embed:
# the anchor is compiled in at configure time from the flag below.
python3 tools/sign_image.py --genkey --output keys/
cmake -B build -DEBLDR_BOARD=<board> -DCMAKE_BUILD_TYPE=Release \
      -DEBLDR_PRODUCTION_KEY=$(cat keys/public_key.hex)
```

### 2.3 Key Storage After Generation

| Storage Location | Content | Access Control |
|---|---|---|
| HSM or air-gapped vault | Private key (`eboot_signing_key.pem`) | Two-person access; encrypted at rest |
| CI/CD signing server | Private key (encrypted) | Build system service account only |
| Source repository | **Never** — private keys must never be committed | N/A |

---

## 3. Public Key Embedding

### 3.1 Compiled-In Key (Default)

The public key is compiled into the bootloader. `eos_keystore_init()`
(`core/keystore.c`) uses it only when the board has no OTP at all — when
`eos_hal_otp_read()` returns `EOS_ERR_NOT_SUPPORTED` because the board port
provides no `otp_read` hook. Today that is every board under `boards/`, so
on every shipped board the compiled-in key *is* the trust anchor.

**Which key gets compiled in is a configure-time decision:**

```bash
# A real key: the raw 32-byte Ed25519 public key as 64 hex characters,
# e.g. the `.pub.raw` file from §2.2 step 3, hex-encoded.
cmake -B build -DEBLDR_BOARD=stm32f4 -DCMAKE_BUILD_TYPE=Release \
      -DEBLDR_PRODUCTION_KEY=3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c
```

`cmake/ProductionKey.cmake` checks the value (exactly 64 hex characters, and
not the development key), generates `build/generated/production_key.c`
defining `ebldr_production_key[]` (declared in `include/eos_production_key.h`),
compiles it into `eboot_core`, and defines `EBLDR_PRODUCTION_KEY` so
`core/keystore.c` uses that symbol in place of its development key.

**Without `EBLDR_PRODUCTION_KEY` the compiled-in key is the development key**
(§7.4), and the build says so with a `#warning` on every compile of
`core/keystore.c`. A **Release build of a real board refuses to configure**
in that state:

```
CMake Error at CMakeLists.txt:56 (message):
  EBLDR_PRODUCTION_KEY is not set: a Release build of board 'stm32f4' would
  compile in the RFC 8032 test key as its trust anchor, and anyone can sign
  for that key. Pass -DEBLDR_PRODUCTION_KEY=<64 hex characters> (the raw
  Ed25519 public key), or, for a bring-up or CI build that will never reach a
  device, -DEBLDR_ALLOW_DEV_KEY=ON.
```

`-DEBLDR_ALLOW_DEV_KEY=ON` is the one way past the gate. It exists for
bring-up on a bench and for CI cross-compiles that only check the tree
builds; it is never passed by `.github/workflows/release.yml`, and
`tests/unit/test_release_workflow_production_key.py` fails if it ever is.
`tests/unit/test_production_key_gate.py` runs real configures against the
tree and pins each of the behaviours above.

**Verification** uses whatever the keystore selected: `eos_image_verify_signature()`
takes the active key from `eos_keystore_get_active_key()` and checks the
Ed25519 signature over the signed header prefix (`EOS_IMG_SIGNED_LEN`).

### 3.2 OTP/eFuse Key Storage

For devices with one-time-programmable memory:

| Field | OTP Address | Size | Description |
|---|---|---|---|
| `pubkey_slot_0` | OTP base + 0x00 | 32 bytes | Primary public key |
| `pubkey_slot_1` | OTP base + 0x20 | 32 bytes | Backup public key |
| `key_revoke_mask` | OTP base + 0x40 | 4 bytes | Bit mask — bit N=1 revokes slot N |
| `security_version` | OTP base + 0x44 | 4 bytes | Monotonic counter for anti-rollback |

**Advantages over compiled-in:**
- Public key cannot be modified by flash reprogramming
- Key revocation is permanent (OTP bits are one-way)
- Survives complete flash erase

### 3.3 Secure Element Key Storage

For high-security deployments using an external secure element (e.g., ATECC608B, OPTIGA Trust M):

| Operation | API |
|---|---|
| Store public key | `se_write_slot(slot_id, pubkey, 32)` |
| Verify signature | `se_verify_ed25519(slot_id, hash, sig)` |
| Read key hash | `se_read_slot_hash(slot_id, hash_out)` |

The secure element performs the signature verification internally — the public key is protected from modification by the SE's tamper-resistant hardware.

---

## 4. Dual-Key Slot Support

eBootloader supports **two key slots** (primary and backup) to enable seamless key rotation without bricking deployed devices.

### 4.1 Key Slot Architecture

```
┌─────────────────────────────────┐
│        Image Header             │
│   sig_type = ED25519            │
│   signature[64]                 │
│   (signed with either key)      │
└──────────────┬──────────────────┘
               │
        ┌──────┴──────┐
        ▼             ▼
  ┌──────────┐  ┌──────────┐
  │ Slot 0   │  │ Slot 1   │
  │ Primary  │  │ Backup   │
  │ pubkey   │  │ pubkey   │
  └──────────┘  └──────────┘
```

### 4.2 Verification Algorithm

The bootloader tries each non-revoked key slot in order:

1. Check `key_revoke_mask` — skip any revoked slots
2. Attempt signature verification with slot 0 public key
3. If slot 0 fails, attempt with slot 1 public key
4. If all slots fail → `EOS_ERR_SIGNATURE`

### 4.3 Slot Assignment Convention

| Slot | Purpose | Typical Usage |
|---|---|---|
| Slot 0 | Primary production key | Signs all production firmware releases |
| Slot 1 | Backup / rotation target | Receives new key during rotation; becomes new primary |

---

## 5. Key Rotation Procedure

Key rotation replaces the active signing key without disrupting deployed devices. The dual-slot architecture ensures that devices can verify firmware signed with either the old or new key during the transition window.

### 5.1 Rotation Timeline

```
Phase 1: Prepare          Phase 2: Transition       Phase 3: Revoke
(1 release cycle)         (2 release cycles)        (after full fleet update)

┌─────────────────┐      ┌─────────────────┐      ┌─────────────────┐
│ Generate new    │      │ Sign firmware   │      │ Revoke old key  │
│ keypair         │      │ with NEW key    │      │ (set revoke bit)│
│                 │      │                 │      │                 │
│ Install new     │      │ Old key still   │      │ Increment       │
│ pubkey in       │      │ accepted for    │      │ security version│
│ slot 1          │      │ verification    │      │                 │
│                 │      │                 │      │ Sign firmware   │
│ Continue signing│      │ Fleet updates   │      │ with new key    │
│ with OLD key    │      │ propagate       │      │ only            │
└─────────────────┘      └─────────────────┘      └─────────────────┘
```

### 5.2 Step-by-Step Procedure

**Phase 1 — Prepare (release N):**

1. Generate new Ed25519 keypair on air-gapped workstation (see §2.2)
2. Update `eos_signing_pubkey_1` in `include/eos_signing_key.h` with new public key
3. Build and sign stage-1 bootloader with existing (old) key
4. Deploy bootloader update — devices now have both public keys
5. Continue signing application firmware with old key (slot 0)

**Phase 2 — Transition (releases N+1, N+2):**

6. Begin signing new application firmware with the **new** key (slot 1)
7. Devices that received the bootloader update verify with slot 1
8. Devices still on old bootloader verify with slot 0 (old firmware still deployed)
9. Monitor fleet update telemetry — track percentage of devices on new bootloader

**Phase 3 — Revoke (release N+3, after fleet convergence):**

10. Verify ≥99% of fleet has updated bootloader with both key slots
11. Set `key_revoke_mask` bit 0 to revoke old key (slot 0)
12. Move new key to slot 0; generate next rotation key in slot 1 (optional)
13. Increment `security_version` monotonic counter
14. Securely destroy old private key material

### 5.3 Rotation Checklist

- [ ] New keypair generated on air-gapped workstation
- [ ] New public key tested in CI with `sign_image.py --verify`
- [ ] Bootloader binary with new public key deployed and confirmed on test fleet
- [ ] Fleet telemetry confirms ≥99% adoption of new bootloader
- [ ] Old key revocation applied (OTP bit or compiled-in flag)
- [ ] Old private key material securely destroyed
- [ ] Key inventory updated in HSM management system
- [ ] Security counter incremented

---

## 6. Key Revocation via Monotonic Counter

### 6.1 Security Version Counter

The security version is a monotonic counter that can only increment. It is stored in:

| Storage | Mechanism | Reversibility |
|---|---|---|
| OTP/eFuse | One-time programmable bits | Irreversible |
| Dedicated flash sector | Counter with anti-tearing write | Reversible (with flash access) |
| Secure element | SE-managed counter | Irreversible |

On STM32F4 the OTP counter is a unary fuse bitmap in the
`security_version` field: the value is the number of cleared (0) bits in
the four bytes at OTP offset `0x144`, so the floor can only rise and each
increment burns exactly one fuse. Four bytes hold 32 steps; see
`docs/secure_boot_chain.md` §4.1. The encoding is the board's
(`boards/stm32f4`); `eos_secure_boot_update_rollback()` advances it
through the monotonic counter rather than writing a value, because OTP
bits only clear and a value write could not raise it.

### 6.2 Revocation Flow

```
Image header: security_counter = 5
Device OTP:   security_version = 5   → ACCEPT (5 ≥ 5)

Image header: security_counter = 4
Device OTP:   security_version = 5   → REJECT (4 < 5, rollback attempt)

Image header: security_counter = 6
Device OTP:   security_version = 5   → ACCEPT (6 ≥ 5)
                                        Update OTP to 6
```

### 6.3 Counter Management Rules

1. **Never decrement.** The counter is monotonic — any attempt to write a lower value is silently ignored.
2. **Increment on key rotation.** Every key rotation bumps the security version by 1.
3. **Increment on critical CVE.** A security-critical fix may bump the counter to prevent rollback to vulnerable versions.
4. **Coordinate with fleet.** Before incrementing, ensure the new firmware is available for all devices.

---

## 7. Production vs. Development Key Separation

### 7.1 Key Environments

| Environment | Key Purpose | Storage | Signing Authority |
|---|---|---|---|
| **Development** | Local builds, unit tests, CI | Plaintext PEM in developer workspace | Any developer |
| **Staging** | Pre-production validation | Encrypted PEM on staging build server | Release engineer |
| **Production** | Release firmware | HSM-protected; two-person authorization | Release manager + security officer |

### 7.2 Enforcement Mechanism

There is one switch, and it is structural rather than advisory:

- `core/keystore.c` compiles the development key under `#ifndef
  EBLDR_PRODUCTION_KEY` and the generated `ebldr_production_key[]` under
  `#else`. The two never coexist in one object.
- `CMakeLists.txt` defines `EBLDR_PRODUCTION_KEY` only when a key was given,
  and refuses a Release build of a real board that gives none (§3.1).
- `.github/workflows/release.yml` passes the `EBLDR_PRODUCTION_KEY_HEX`
  repository secret on every board configure; with the secret unset the value
  is empty and the configure fails closed. After every firmware build it scans
  each `.elf`, `.bin`, `.a` and `.o` under `build/` for the development key's
  32 bytes and fails the job on a hit.

Staging keys are not modelled: a staging build is a production-shaped build
configured with a staging public key.

### 7.3 Development Key Policy

| Rule | Rationale |
|---|---|
| Development private key is **committed** to the repository | Enables any developer to build and test signed images locally |
| Development key does not reach a production artifact **built through the gate** | Any build of a real board that is not `CMAKE_BUILD_TYPE=Debug` -- `Release`, `MinSizeRel`, `RelWithDebInfo`, any spelling, or no build type at all -- refuses to configure without `EBLDR_PRODUCTION_KEY` unless `EBLDR_ALLOW_DEV_KEY=ON` is passed explicitly; the key it accepts is checked to be a point in the prime-order subgroup (`tools/check_production_key.py`); and the release workflow validates the secret before any board is configured and scans every artifact for the development key's bytes (§3.1, §7.2). What the gate cannot see: a Debug build flashed to a device, a build that passes `EBLDR_ALLOW_DEV_KEY=ON`, or a fork that removes the gate. Those are policy, not mechanism. |
| Production key **never** appears in source control | Only the public key is embedded; private key stays in HSM |
| The stored `EBLDR_PRODUCTION_KEY_HEX` is checked against the generated `.pub` **before** it is stored | The configure-time and release-time checks refuse a key that is off the curve, of low order, or outside the prime-order subgroup -- but about **one in sixteen** single-hex-digit typos of a key lands on a *different valid key* inside the subgroup, which no check on the key alone can distinguish from the real one. That rate is derivable, not measured: a mutated encoding decodes to a curve point with probability about 1/2 and lands in the prime-order subgroup with probability 1/8. Measured counts for specific keys vary around it -- 52/960 for the development key, 64/960 for the RFC 8032 TEST 2 key, 75/960 for TEST 3. A device built on it refuses every image, with a green build. The only control for that case is comparing the secret to the `.pub` the key generator wrote, by eye or by `cmp`, before it enters the secret store. |
| CI pipeline uses **staging** key for integration tests | Tests signature verification without exposing production key |

### 7.4 Well-Known Development Key

The development key is the RFC 8032 section 7.1 TEST 1 key pair. Its secret
scalar is printed in the RFC, so images signed with it are not authenticated
by anyone in particular:

```
Public key (compiled in by core/keystore.c without EBLDR_PRODUCTION_KEY):
  d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a
Secret (RFC 8032 §7.1 TEST 1):
  9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60

⚠️  THIS KEY IS PUBLIC. Images signed with this key are NOT authenticated.
```

`tools/gen_fw_update_test_sigs.py` signs the unit-test fixtures with it, and
`tests/unit/test_keystore.c` checks that the compiled-in bytes are exactly
this key (an earlier revision of the array was off the curve, so nothing
could verify against it at all). `cmake/ProductionKey.cmake` refuses it as a
value for `EBLDR_PRODUCTION_KEY`.

---

## 8. Emergency Key Compromise Response

### 8.1 Compromise Classification

| Severity | Scenario | Response Time |
|---|---|---|
| **P0 — Confirmed compromise** | Private key material exposed publicly or to unauthorized party | Immediate (< 4 hours) |
| **P1 — Suspected compromise** | Unauthorized access to signing infrastructure detected | < 24 hours |
| **P2 — Precautionary** | Personnel change, infrastructure migration, policy violation | < 7 days |

### 8.2 P0 Response Procedure

```
┌─ HOUR 0 ──────────────────────────────────────────────┐
│ 1. Notify security team: security@embeddedos.org      │
│ 2. Revoke compromised key in CI/CD pipeline           │
│ 3. Halt all firmware signing with compromised key     │
└───────────────────────────────────────────────────────┘
         │
┌─ HOURS 1-4 ───────────────────────────────────────────┐
│ 4. Generate emergency replacement keypair (§2.2)      │
│ 5. Build emergency bootloader with new public key     │
│ 6. Sign emergency bootloader with backup key (slot 1) │
│ 7. Push emergency OTA to all connected devices        │
└───────────────────────────────────────────────────────┘
         │
┌─ HOURS 4-24 ──────────────────────────────────────────┐
│ 8. Increment security version counter                 │
│ 9. Revoke compromised key slot (OTP/eFuse)            │
│ 10. Monitor fleet for devices still on old key        │
│ 11. Publish security advisory                         │
└───────────────────────────────────────────────────────┘
         │
┌─ DAYS 1-7 ────────────────────────────────────────────┐
│ 12. Root cause analysis                               │
│ 13. Update key management procedures                  │
│ 14. Rotate all related credentials                    │
│ 15. Post-incident review                              │
└───────────────────────────────────────────────────────┘
```

### 8.3 Key Compromise Indicators

- Firmware images signed with the production key that were not produced by the authorized build pipeline
- Unauthorized access logs on the HSM or signing server
- Public disclosure of key material (paste sites, repositories, social media)
- Anomalous device behavior consistent with unauthorized firmware

### 8.4 Limitations

- **Compiled-in keys:** Devices with compiled-in public keys require a bootloader update to rotate keys. If both key slots are compromised and the device cannot receive OTA updates, physical UART recovery is required.
- **OTP/eFuse keys:** Once all OTP key slots are revoked, the device cannot verify any firmware. This is a permanent brick condition — plan key slots carefully.
- **Offline devices:** Devices not connected to the network cannot receive emergency key rotation. They remain vulnerable until physically recovered.

---

## References

- [Secure Boot Chain](secure_boot_chain.md)
- [Threat Model](threat_model.md)
- [Security Model](security.md)
- [Architecture](architecture.md)
- RFC 8032 — Edwards-Curve Digital Signature Algorithm (EdDSA)
