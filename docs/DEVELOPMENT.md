# Development

## Supported environment

Aurore currently targets Windows x64 with Visual Studio 2026, the MSVC v145 toolset, CMake, PowerShell, and Git. The solution uses the latest C++ language mode supported by that toolchain.

Linux and macOS backends are possible future work, but neither is currently promised or tested.

## Clone and build

```powershell
git clone --recurse-submodules https://github.com/ZettaBite4031/Aurore.git
cd Aurore
./scripts/build.ps1 -Configuration Debug
```

The build script:

1. initializes exact submodule revisions;
2. configures and builds Sonnet as a shared library;
3. configures and builds GoogleTest;
4. builds `Aurore.slnx` with MSBuild;
5. copies `sonnet.dll` beside the executables;
6. runs `Aurore.Tests.exe` and writes GoogleTest XML to `test-results`.

Useful options:

```powershell
./scripts/build.ps1 -Configuration Release
./scripts/build.ps1 -Configuration Debug -SkipDependencies
./scripts/build.ps1 -Configuration Debug -SkipTests
```

The CI workflow calls the same script for Debug and Release so the documented local path and clean-runner path cannot quietly diverge.

## Starting from an exported source archive

An ordinary archive cannot preserve Git submodule pointers. Before the first commit in a brand-new repository, initialize Git and restore the two dependency entries:

```powershell
git init
git submodule add https://github.com/ZettaBite4031/Sonnet.git vendor/Sonnet
git -C vendor/Sonnet checkout 0d07d323cf27a71058d30552928b138847e5905c
git submodule add https://github.com/google/googletest.git vendor/googletest
git -C vendor/googletest checkout a0f06a70e3da7afa88da9527c43951bca1f7cef2
git add .
```

Do not replace those pins with current upstream heads without a deliberate dependency update and complete test run.

## Configuration

The runtime reads `config/aurore.json` relative to its working directory. When no file exists, Core writes its defaults. Active settings are grouped under `Network` and `Clients`; `Server`, `Logging`, and `World` currently reserve user-facing configuration structure for their owning systems.

All capacity and timeout values must be positive. Invalid types or values fail initialization instead of being silently coerced.

## Code style

- Use PascalCase for types and functions and `m_` for members.
- Use tabs for C++ indentation and spaces for project, JSON, Markdown, and PowerShell files.
- Put includes in subsystem, standard-library, and platform groups when that improves scanning.
- Keep ownership explicit. Prefer values, references with clear lifetime, and move-only transfer over shared ownership.
- Avoid deep inheritance and virtual dispatch unless runtime substitution is a real requirement.
- Group related concepts into cohesive files; do not split every small type into its own pair.
- Comment invariants, external constraints, and surprising choices.

`.editorconfig` records the mechanical portion of this style.

## Tests

The suite is separated by behavior: byte and NBT primitives, packet framing and codecs, protocol sessions and pipelines, Configuration sequencing, registries and snapshots, client lifecycle, network contracts, IOCP backend behavior, and full loopback paths.

For a change:

1. add or update the narrow unit/component test;
2. add a higher-layer regression when the defect crossed a boundary;
3. run Debug and Release;
4. preserve deterministic resource cleanup in loopback tests.

Manual vanilla-client runs provide interoperability evidence, not a substitute for automated regression tests.

## Dependency and project-file maintenance

Visual Studio projects list source files explicitly. When adding or moving a `.cpp` or public header, update both the `.vcxproj` and `.vcxproj.filters` files. Keep project dependencies consistent with the direction documented in [Architecture](ARCHITECTURE.md).

Dependency pins and license notes live in [Third-party notices](../THIRD_PARTY_NOTICES.md).
