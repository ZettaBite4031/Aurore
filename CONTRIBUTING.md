# Contributing to Aurore

Thanks for being interested in Aurore! This project is still young, and the best contributions make one working path more reliable or understandable rather than adding a lot of machinery around hypothetical future needs.

## Before you start

Read the [README](README.md), [Architecture](docs/ARCHITECTURE.md), [Development](docs/DEVELOPMENT.md), and any relevant sections of [Protocol and Data](docs/PROTOCOL_AND_DATA.md). If your change affects external data or rights-sensitive material, please also read [Legal and Provenance](docs/LEGAL_AND_PROVENANCE.md).

For substantial changes, consider opening a GitHub issue before writing a large patch. I'd rather discuss a good idea early than ask someone to rework a week of code after the fact.

## Working principles

- Keep subsystem ownership explicit: Network transports bytes; Protocol interprets wire data; Core applies policy; World owns simulation; Data imports external inputs.
- Validate untrusted data before letting it affect authoritative state.
- Preserve bounded memory, queue, packet, process, and timeout behavior.
- Prefer observable ordering and clear state transitions over hidden callback chains.
- Write tests that would catch a future regression, including failure paths.
- Keep bug fixes, refactors, and new features separate when practical.
- Update documentation when behavior, configuration, or the public contract changes.

Aurore cares about **vanilla fidelity**. Please don't remove odd but observable behavior merely because a cleaner or faster replacement looks attractive. If an optimization changes compatibility, explain it and propose a testable policy choice.

## Coding style

Use modern C++23, matching the surrounding source:

- PascalCase for types and functions; `m_` for members.
- Tabs for C++ indentation; conventional spaces for Markdown, CMake, JSON, YAML, and scripts.
- Explicit ownership and lifetime; avoid unnecessary shared mutable state.
- Small cohesive groups of files rather than one pair per trivial type.
- Comments for invariants and surprising design decisions, not line-by-line retellings.
- New abstractions should solve a concrete recurring problem, not just look elegant.

The project uses `.editorconfig` for mechanical conventions. CMake is the build-system source of truth; add new source files to the relevant subsystem `CMakeLists.txt`.

## Builds and tests

Both Windows x64 and Linux x86-64 are supported. Work toward a green matrix in Debug and Release:

```text
windows-debug    windows-release
linux-debug      linux-release
```

The configured toolchain must support Aurore's C++23 standard-library requirements. See [Development](docs/DEVELOPMENT.md) for exact presets and platform dependencies.

When changing a feature, add the narrowest unit test that proves it, plus a higher-level regression if the bug crossed subsystem boundaries. Native-socket loopback tests are particularly useful for lifecycle work. The optional `AURORE_TEST_STRESS=1` environment variable enables longer stress cases; it is not a replacement for the default suite.

Before opening a pull request:

1. Build and test in Debug and Release on your available platform.
2. Confirm the change contains no build outputs, caches, game assets, generated datasets, or personal information.
3. Explain the behavior changed, why it changed, and how it was tested.
4. Check that related documentation still tells the truth.

CI verifies the Windows/Linux build matrix; manual testing with a vanilla client remains valuable for interoperability.

## Independent implementation and sources

Do **not** submit copied, decompiled, or mechanically translated Mojang implementation code, official assets, complete extracted Minecraft datasets, credentials, or material without clear permission to redistribute. The repository should contain our own implementation and original test fixtures. Compatibility research should be described as observed inputs, outputs, packet order, failure behavior, and other implementation-independent facts.

Use an Aurore-owned test namespace (for example, `aurore_test`) for synthetic fixtures. Do not add real user packet captures or account details to tests or issue reports.

## Review culture

The point is to build something interesting and learn from it. Thoughtful questions are welcome. It's fine to disagree about architecture or performance; concrete reasoning and reproducible experiments help more than arguing by preference.

First Light is intentionally narrow. Gameplay and larger platform features belong in later vertical slices unless you're fixing a demonstrated First Light regression. The [roadmap](docs/ROADMAP.md) keeps that boundary visible.

For questions, use [GitHub Issues](https://github.com/ZettaBite4031/Aurore/issues), Discord `@mirathatnardo`, or Telegram `@mirathatnardo`. A project email address has not been configured yet.
