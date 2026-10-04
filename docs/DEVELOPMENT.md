# Development

Aurore uses CMake as its canonical build system. The same project graph is exercised on Windows and Linux, in Debug and Release, by local presets and GitHub Actions.

The current portability boundary is deliberate:

- **Windows x64** is a functional runtime target using the IOCP network backend.
- **Linux x86-64** is a supported build-and-test target while the epoll backend is under development.
- **macOS** is represented in the platform/backend contract but is not currently a supported build or runtime target.

## Toolchain baseline

Aurore requires:

- CMake 3.25 or newer;
- Git with submodule support;
- a C++23-capable compiler and standard library.

The configured toolchain must support the C++23 facilities Aurore already uses, including `std::expected` and `std::format` with chrono formatting. CMake checks this during configuration and fails early when the selected compiler/standard-library pair is insufficient.

### Windows

The canonical Windows presets currently target:

- Windows x64;
- Visual Studio 2026;
- MSVC v145.

PowerShell is only required for the optional `scripts/build.ps1` convenience wrapper. It is not the build-system source of truth.

### Linux

The canonical Linux presets currently target:

- x86-64 Linux;
- Ninja;
- GCC or Clang with sufficient C++23 standard-library support.

On Debian/Ubuntu/Linux Mint systems, the baseline tools are typically available with:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build git
```

A newer compiler may be required when the distribution's default standard library lacks a C++23 facility used by Aurore.

## Clone

Clone the repository and its pinned source dependencies together:

```bash
git clone --recurse-submodules https://github.com/ZettaBite4031/Aurore.git
cd Aurore
```

If the repository was cloned without submodules:

```bash
git submodule update --init --recursive
```

Do not replace pinned dependency revisions with upstream branch heads as part of unrelated work.

## CMake presets

`CMakePresets.json` is the shared developer and CI interface.

The current configure/build/test presets are:

| Preset | Platform | Configuration | Network runtime |
|---|---|---|---|
| `windows-debug` | Windows x64 | Debug | IOCP |
| `windows-release` | Windows x64 | Release | IOCP |
| `linux-debug` | Linux x86-64 | Debug | No native backend yet |
| `linux-release` | Linux x86-64 | Release | No native backend yet |

Linux currently configures with `AURORE_ALLOW_MISSING_PLATFORM_BACKEND=ON`. This is an explicit portability exception, not a production behavior. Once the epoll backend satisfies the network contract, that exemption should be removed from the Linux presets.

`CMakeUserPresets.json` is intentionally ignored and may be used for machine-local compiler paths or developer-specific preset inheritance.

## Build and test

### Linux

Debug:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug --parallel
ctest --preset linux-debug
```

Release:

```bash
cmake --preset linux-release
cmake --build --preset linux-release --parallel
ctest --preset linux-release
```

The convenience wrapper performs the same sequence:

```bash
./scripts/build.sh Debug
./scripts/build.sh Release
```

Tests may be skipped during local iteration:

```bash
./scripts/build.sh Debug --skip-tests
```

A review-ready change should still run the full suite.

### Windows

Debug:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel
ctest --preset windows-debug
```

Release:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release --parallel
ctest --preset windows-release
```

The PowerShell wrapper provides the same workflow:

```powershell
./scripts/build.ps1 -Configuration Debug
./scripts/build.ps1 -Configuration Release
```

For iteration only:

```powershell
./scripts/build.ps1 -Configuration Debug -SkipTests
```

The wrapper contains no independent project/dependency graph. CMake remains authoritative.

## Build outputs

Preset build trees live under:

```text
out/build/<preset>/
```

Executables and shared runtime libraries are emitted under the preset's `bin/` directory, while static/import libraries are emitted under `lib/`.

For example:

```text
out/build/linux-debug/bin/Aurore.Server
out/build/linux-debug/bin/Aurore.Tests
```

Build trees, generated CMake state, local caches, and runtime configuration are ignored by Git.

## Dependencies

Aurore currently carries source dependencies as exact Git submodule revisions:

- `vendor/Sonnet`
- `vendor/googletest`

The root CMake project adds these dependencies directly to the build graph. They are not expected to be independently configured or prebuilt by the developer.

Sonnet tests/install rules are disabled for the Aurore build. GoogleTest participates only when `AURORE_BUILD_TESTS` is enabled.

Dependency updates are intentional changes. A revision update should include:

1. the new exact submodule revision;
2. license/provenance review where necessary;
3. successful Windows Debug/Release builds and tests;
4. successful Linux Debug/Release builds and tests.

See [Third-party notices](../THIRD_PARTY_NOTICES.md).

## Platform and backend contract

CMake interprets the target platform once and generates:

```text
Aurore/Build/Config.hpp
```

inside the build tree.

Aurore-owned code uses that generated contract for supported platform/backend decisions rather than spreading compiler-specific platform detection throughout the codebase.

The current compile-time backend mapping is:

```text
Windows  -> IOCP
Linux    -> epoll pending
macOS    -> kqueue reserved for future work
```

Production configurations are expected to have exactly one native network backend. The missing-backend build option exists only to allow Linux portability work before epoll is complete.

When adding a backend:

- keep OS-specific implementation files out of unrelated platform builds;
- preserve the `NetworkBackend` observable lifecycle and queue/resource contracts;
- keep Minecraft semantics out of `Aurore.Network`;
- make `Automatic` select the native backend for that platform;
- run shared transport tests against every real backend where practical.

## Configuration

The runtime reads:

```text
config/aurore.json
```

relative to its working directory.

If the file does not exist, Core creates a default configuration automatically.

Active settings are grouped primarily under `Network` and `Clients`; additional top-level areas reserve configuration ownership for their corresponding systems.

Invalid setting types, invalid backend names, zero capacities, or otherwise inconsistent limits fail initialization rather than being silently coerced.

On Linux today, a successfully built server will fail networking initialization with `BackendUnavailable` because the epoll backend has not yet been implemented. That is the expected portability-baseline behavior.

## Tests

The suite is divided by behavior rather than by project-file convenience.

Current coverage includes:

- byte-buffer and NBT primitives;
- resource locations, UUID/data values, registries, tags, and snapshots;
- packet framing and codecs;
- protocol sessions, pipelines, Login, and Configuration behavior;
- Core client lifecycle and data foundations;
- platform-neutral network queues, resource accounting, configuration, and fake-backend lifecycle;
- Windows IOCP backend internals;
- Windows real-socket loopback paths.

Platform-neutral `NetworkTests.cpp` is built on both Windows and Linux. IOCP-internal and Winsock loopback tests are included only when the IOCP backend is compiled.

As epoll is implemented, Linux should gain the same shared behavioral expectations rather than a separate weaker test contract.

For a change:

1. add or update the narrowest meaningful unit/component test;
2. add a higher-layer regression when the defect crossed a subsystem boundary;
3. run Debug and Release on the platform you are developing on;
4. rely on CI to validate the other supported platform;
5. preserve deterministic resource cleanup in transport and loopback tests.

Manual vanilla-client interoperability runs are evidence, not a replacement for automated regression coverage.

## Continuous integration

`.github/workflows/ci.yml` runs on pushes and on pull requests targeting `main`.

The current matrix validates:

```text
Windows x64 Debug
Windows x64 Release
Linux x86-64 Debug
Linux x86-64 Release
```

Each job performs:

```text
configure
   ↓
build
   ↓
test
```

A configuration, compiler, linker, or test failure fails that matrix job.

CI should remain structurally close to the documented local workflow. Avoid adding a second CI-only build path unless there is a concrete reason.

## Adding or moving source files

CMake is the only project-file source of truth.

When adding or moving a source/header:

1. update the owning subsystem's `CMakeLists.txt`;
2. keep target dependencies consistent with [Architecture](ARCHITECTURE.md);
3. do not add dependencies simply because another target happened to expose an include path;
4. keep platform-specific source selection inside the owning subsystem's CMake file.

There are no hand-maintained Visual Studio `.slnx` or `.vcxproj` files. Visual Studio consumes the CMake project instead.

## Code style

- Use PascalCase for types and functions and `m_` for members.
- Use tabs for C++ indentation and spaces for CMake, JSON, Markdown, YAML, and PowerShell.
- Put includes into subsystem, standard-library, and platform groups when that improves scanning.
- Keep ownership explicit. Prefer values, references with clear lifetime, and move-only transfer over shared ownership.
- Avoid deep inheritance and virtual dispatch unless runtime substitution is a real requirement.
- Group related concepts into cohesive files rather than splitting every small type into its own pair.
- Comment invariants, external constraints, and non-obvious choices rather than narrating individual lines.
- Use `NDEBUG`/the standard `assert` contract for portable debug assertions rather than compiler-specific debug macros.

`.editorconfig` records the mechanical portion of the style.

## Starting from an exported source archive

An ordinary source archive does not preserve live Git submodule metadata.

When reconstructing a repository from an exported archive, initialize Git and restore the dependencies at the revisions documented by the repository:

```bash
git init

git submodule add \
    https://github.com/ZettaBite4031/Sonnet.git \
    vendor/Sonnet

git submodule add \
    https://github.com/google/googletest.git \
    vendor/googletest
```

Then check out the exact revisions recorded by the source package or `THIRD_PARTY_NOTICES.md`.

Do not silently replace those pins with current upstream heads.

## Review-ready checklist

Before considering a change ready to merge:

```text
[ ] Correct subsystem owns the behavior
[ ] New/changed source appears in the correct CMake target
[ ] Platform-specific code is isolated from unrelated platforms
[ ] Debug build passes
[ ] Release build passes
[ ] Relevant tests cover success and failure paths
[ ] CI is green on Windows and Linux
[ ] Generated output / local runtime data is not committed
[ ] Public documentation matches any changed contract or workflow
```

For broader architectural expectations, read [Architecture](ARCHITECTURE.md) before introducing a new cross-layer dependency or abstraction.
