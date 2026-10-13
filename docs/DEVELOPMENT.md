# Development

This is the practical guide to building, running, debugging, and testing Aurore. It covers **First Light**, the current C++23 server implementation.

## Supported platforms and toolchains

| Platform | Runtime backend | Toolchain | Configuration |
|---|---|---|---|
| Windows x64 | IOCP | Visual Studio 2026, MSVC v145 C++ tools | Debug and Release |
| Linux x86-64 | epoll | GCC or Clang with adequate C++23 library support | Debug and Release |

CMake **3.25+** and Git with submodule support are required. Linux builds use Ninja. The CMake configure step checks for the specific C++23 features Aurore uses, including `std::expected` and `std::format` with chrono formatting. A compiler claiming `-std=c++23` is not sufficient if its standard library lacks these features.

On Debian/Ubuntu-family systems, start with:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build git libcurl4-openssl-dev
```

A newer compiler or CMake may be needed than those supplied by some distribution releases. Linux also needs the libcurl runtime library when running the server, and binary portability depends on the target system's C/C++ runtime compatibility.

macOS and other platforms are **not supported** in First Light. The build has reserved platform/backend concepts for future porting, but no working macOS production network backend.

## Get the source

```bash
git clone --recurse-submodules https://github.com/ZettaBite4031/Aurore.git
cd Aurore
```

For a specific tagged version, add `--branch v0.1.0-first-light` after the tag is published. If you cloned without dependencies:

```bash
git submodule update --init --recursive
```

The two source dependencies are Sonnet (JSON) and GoogleTest. CMake builds them as part of the project. You do not need to configure them separately.

## Configure, build, test

The canonical entry points are the four CMake presets in `CMakePresets.json`:

| Preset | Platform | Build configuration |
|---|---|---|
| `windows-debug` | Windows x64 | Debug |
| `windows-release` | Windows x64 | Release |
| `linux-debug` | Linux x86-64 | Debug |
| `linux-release` | Linux x86-64 | Release |

**Windows (PowerShell):**

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel
ctest --preset windows-debug

cmake --preset windows-release
cmake --build --preset windows-release --parallel
ctest --preset windows-release
```

**Linux (shell):**

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug --parallel
ctest --preset linux-debug

cmake --preset linux-release
cmake --build --preset linux-release --parallel
ctest --preset linux-release
```

Wrappers are available for the same process:

```powershell
./scripts/build.ps1 -Configuration Debug
./scripts/build.ps1 -Configuration Release
./scripts/build.ps1 -Configuration Debug -SkipTests
```

```bash
./scripts/build.sh Debug
./scripts/build.sh Release
./scripts/build.sh Debug --skip-tests
```

The scripts provide `-SkipTests` / `--skip-tests`, **not** a `-SkipDependencies` switch. CMake remains responsible for the dependency graph.

### Paths and CMake options

Each preset builds into `out/build/<preset>/`; executables and shared libraries are placed in its `bin/` directory. For example:

```text
out/build/windows-release/bin/Aurore.Server.exe
out/build/linux-release/bin/Aurore.Server
out/build/linux-release/bin/libsonnet.so
```

Actual shared-library filenames may vary by toolchain. `AURORE_BUILD_TESTS=ON` enables GoogleTest and the Aurore test executable. `AURORE_ALLOW_MISSING_PLATFORM_BACKEND=ON` is a **porting-only** escape hatch; do not use it for production server packages.

`out/`, `cache/`, local-generated data, and test results are ignored by Git. They do not belong in commits or source archives.

## Run First Light

The production entry point is `Aurore.Server`. Run it from the directory that should contain the runtime data:

```powershell
# From the repository root, after a Windows Release build:
./out/build/windows-release/bin/Aurore.Server.exe
```

```bash
# From the repository root, after a Linux Release build:
./out/build/linux-release/bin/Aurore.Server
```

Aurore uses the **current working directory** for `config/aurore.json` and the default `cache/minecraft/` location. A missing configuration file is created from built-in defaults.

For isolated testing, launch from a separate directory or extract an official binary package there. Do not rely on the executable's directory being the working directory.

By default, the server uses TCP port **25565** and binds to **`0.0.0.0`**. If you're testing locally, change `Network.BindAddress` to `127.0.0.1`, then connect with an unmodified **Minecraft Java Edition 1.21.11** client. Don't expose this offline-mode proof of concept as an authenticated public server.

### First-run Minecraft data preparation

Aurore needs an official-source Minecraft 1.21.11 corpus to complete vanilla Configuration. On a normal first run, production startup:

1. Resolves upstream version metadata and official server artifact identity.
2. Downloads the required artifact separately on the user's machine when needed.
3. Verifies its SHA-1 and runs the upstream data-generation process using Java.
4. Validates and caches the generated reports and Minecraft data.
5. Imports the corpus into Aurore's own immutable registry snapshot before accepting players.

A valid cached generation can be reused on later starts. Java is not used to host Aurore itself, but a compatible Java installation is required for new upstream data generation. Install a Java runtime supported by the targeted Minecraft server artifact; see the official Minecraft requirements when setting up a new machine.

To use a specific Java executable:

```powershell
$env:AURORE_JAVA_EXECUTABLE = 'C:\path\to\java.exe'
```

```bash
export AURORE_JAVA_EXECUTABLE=/path/to/java
```

To bypass acquisition/generation using an existing prepared corpus, point to its **SHA-1-named generation root**, not merely its parent directory:

```powershell
$env:AURORE_MINECRAFT_DATA_ROOT = 'C:\cache\minecraft\generated\0123456789abcdef0123456789abcdef01234567'
```

```bash
export AURORE_MINECRAFT_DATA_ROOT=/path/to/generated/0123456789abcdef0123456789abcdef01234567
```

The path must contain `reports/registries.json` and `data/minecraft/`. The hash shown is illustrative, not an actual known valid Minecraft artifact. Incorrect or incomplete data fails startup rather than silently proceeding.

No Minecraft JAR, official assets, or generated game-data corpus should be committed or bundled into release archives. See [Protocol and Data](PROTOCOL_AND_DATA.md) and [Legal and Provenance](LEGAL_AND_PROVENANCE.md).

## Runtime configuration

The default runtime file is `config/aurore.json` relative to the working directory. The sample is tracked at `Aurore.Server/config/aurore.json`.

| Section | First Light uses |
|---|---|
| `Server` | Display/server name |
| `Network` | Backend selection, listening address/port, connection and queue byte limits |
| `Clients` | Maximum connected clients, player cap, protocol timeouts |
| `Logging` | Log activation and verbosity |
| `World` | View-distance configuration placeholder; First Light still sends a fixed initial neighborhood |

The native backend is selected using `Network.Backend: "Automatic"`; use `Iocp` on Windows or `Epoll` on Linux when selecting explicitly. Invalid backend names and inconsistent limit values fail configuration or initialization rather than being guessed at.

Changing the view-distance setting does **not** enable general chunk streaming or world generation in First Light. The actual bootstrap is intentionally fixed.

## Tests and diagnostics

The suite spans byte/NBT primitives, UUIDs and registries, Protocol framing and state transitions, Configuration, Login, data acquisition/import, native backend internals, real-socket loopback, Play input, synchronization, timeouts, player lifecycle, and stress scenarios.

Run all tests with `ctest --preset <name>` or the matching wrapper. The default suite covers the normal test workload; for extended session/reconnection stress:

```powershell
$env:AURORE_TEST_STRESS = '1'
ctest --preset windows-release
Remove-Item Env:AURORE_TEST_STRESS
```

```bash
AURORE_TEST_STRESS=1 ctest --preset linux-release
```

The extended profile increases selected stress-test iteration counts. It does not replace manual vanilla-client testing or guarantee the absence of future regressions.

When reporting a client interoperability issue, include the Aurore commit/tag, client version, last successful protocol stage, expected/observed behavior, minimal reproduction, and a redacted log. Never share account tokens, personal IP addresses, or private captures.

## CI

`.github/workflows/ci.yml` builds and tests the four supported preset configurations on pushes and pull requests. It also uploads short-lived build artifacts for diagnostic use. **Those workflow artifacts are not curated release packages.** Release files need their own staging, dependency review, and clean-machine checks; see [Releasing](RELEASING.md).

## Source layout and style

The individual `Aurore.*` directories contain subsystem-local CMake target declarations. Keep platform-specific sources selected by the owning CMake file. Public headers belong under `include/Aurore/<Subsystem>/`; implementations belong under `src/`.

C++ uses PascalCase for types and functions, `m_` for member fields, and tabs for indentation. Leave comments for invariants and surprising reasoning, not a running commentary of straightforward code. The canonical formatting hints are in `.editorconfig`.

Remember that `WorldManager` contains future-facing simulation phases that are still no-ops. A named function isn't proof that gameplay exists.

## Troubleshooting

- **Missing submodule/CMake target:** `git submodule update --init --recursive`.
- **C++23 configure probe fails:** check both compiler and standard-library versions; in particular, chrono formatting support.
- **Linux CURL not found:** install the distribution's libcurl development package (`libcurl4-openssl-dev` on Ubuntu/Debian).
- **Server cannot start data preparation:** verify upstream connectivity, Java compatibility, cache write permissions, or the explicit existing corpus root.
- **Connection fails or client disconnects on interaction:** verify exact client version and consult [First Light limitations](../README.md#what-is-not-implemented). Unsupported gameplay is expected in this milestone.
- **Server binds to unintended interfaces:** change `Network.BindAddress` before launching, and verify firewall rules.

For how the layers fit together, read [Architecture](ARCHITECTURE.md).
