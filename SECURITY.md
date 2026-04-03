# Security Policy

## Supported Versions
| Version | Supported |
|---------|-----------|
| 0.1.x   | Yes       |

## Reporting a Vulnerability
Report to: security@embeddedos.org
- Do NOT open public issues for vulnerabilities
- Response within 48 hours
- CVE assignment for confirmed issues

## Security Contact SLA

| Stage | Timeline | Description |
|---|---|---|
| Acknowledgment | 48 hours | Initial response confirming receipt of report |
| Triage | 7 days | Severity classification and impact assessment |
| Fix (Critical) | 30 days | Patch available for critical vulnerabilities |
| Fix (High) | 60 days | Patch available for high-severity vulnerabilities |
| Fix (Medium/Low) | 90 days | Patch included in next scheduled release |

## Coordinated Disclosure

eBootloader follows a **90-day coordinated disclosure** policy:

1. Reporter submits vulnerability to security@embeddedos.org
2. Team acknowledges within 48 hours
3. Team triages and assigns severity within 7 days
4. Team develops and tests fix within the SLA window (see above)
5. Fix is released and CVE is published
6. Reporter may publicly disclose after 90 days from initial report, or when the fix is released — whichever comes first
7. If a fix requires more than 90 days, the team will negotiate an extension with the reporter

## Supported Cryptographic Algorithms

| Algorithm | Purpose | Key Size | Standard |
|---|---|---|---|
| **SHA-256** | Image integrity hash | 256-bit digest | FIPS 180-4 |
| **Ed25519** | Firmware signature verification | 256-bit key | RFC 8032 |
| **AES-256-GCM** | Payload encryption (Phase 3) | 256-bit key | NIST SP 800-38D |
| **CRC32** | Boot control block integrity (Phase 1) | 32-bit | IEEE 802.3 |

## Security Features Overview

| Feature | Phase | Status | Description |
|---|---|---|---|
| CRC32 integrity checks | Phase 1 | ✅ Implemented | Image and boot control block integrity |
| Version comparison | Phase 1 | ✅ Implemented | `eos_image_check_version()` API |
| Redundant metadata | Phase 1 | ✅ Implemented | Dual boot control block with CRC |
| Anti-tearing writes | Phase 1 | ✅ Implemented | Power-safe metadata updates |
| SHA-256 image hash | Phase 2 | 🔲 Planned | Full cryptographic hash for image verification |
| Ed25519 signatures | Phase 2 | 🔲 Planned | Digital signature verification of firmware |
| Anti-rollback counters | Phase 2 | 🔲 Planned | Monotonic counter prevents version downgrade |
| Dual key slots | Phase 2 | 🔲 Planned | Primary + backup key for seamless rotation |
| AES-256-GCM encryption | Phase 3 | 🔲 Planned | Encrypted firmware payloads |
| Recovery authentication | Phase 3 | 🔲 Planned | Challenge-response for UART recovery |
| Debug lock | Phase 3 | 🔲 Planned | JTAG/SWD disabled in production builds |
| MPU isolation | Phase 3 | 🔲 Planned | Memory protection between boot stages |

## Standards
- ISO/IEC 20243 — supply chain security
- ISO/IEC/IEEE 15288:2023 — secure development lifecycle
- SPDX license headers on all source files
- CycloneDX SBOM available in ebuild monorepo

## Detailed Security Documentation

For comprehensive security information, see:

- **[Security Model](docs/security.md)** — Phased security approach, design rules, implementation details
- **[Threat Model](docs/threat_model.md)** — STRIDE-based threat analysis with risk ratings and mitigations
- **[Secure Boot Chain](docs/secure_boot_chain.md)** — Complete chain of trust from ROM to application
- **[Key Lifecycle](docs/key_lifecycle.md)** — Ed25519 key generation, rotation, and revocation procedures
- **[mcuboot Comparison](docs/mcuboot_comparison.md)** — Design decisions relative to mcuboot
- **[Security Review Checklist](docs/security_review_checklist.md)** — PR review checklist for security-critical code
