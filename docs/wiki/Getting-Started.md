# Getting Started

## Repository purpose

[](https://github.com/embeddedos-org/eBoot/actions/workflows/ci.yml) [](https://github.com/embeddedos-org/eBoot/actions/workflows/codeql.yml) [](https://github.com/embeddedos-org/eBoot/actions/workflows/scorecard.yml) [](https://github.com/embeddedos-org/eBoot/actions/workflows/release.yml) [](LICENSE)

## First steps

1. Read the [README](https://github.com/embeddedos-org/eBoot/blob/master/README.md) for the project's supported setup and usage path.
2. Clone the repository and enter its directory:

```bash
git clone https://github.com/embeddedos-org/eBoot.git
cd eBoot
```

3. Check the root project inputs below before installing dependencies or selecting a build tool.
4. Review [Development](Development) before changing code, and [Security](Security) before reporting a vulnerability.

## Root project inputs

- `CMakeLists.txt`: CMake build definition.
- `Dockerfile`: Container build definition.
- `requirements.txt`: Python requirements.

## Scope note

The default branch inspected for this page was `master` at [`b42935469821`](https://github.com/embeddedos-org/eBoot/commit/b42935469821343b972738690e3d3a925c223edf). This page intentionally does not invent a universal build command when the repository's own documentation does not provide one.
