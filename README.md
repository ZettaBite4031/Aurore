<div align="center">

# Aurore

### A Minecraft-compatible server, built from the wire up in C++23.

**Protocol-first · Explicit ownership · Bounded resources · Vanilla fidelity**

[![Build and Test](https://github.com/ZettaBite4031/Aurore/actions/workflows/ci.yml/badge.svg)](https://github.com/ZettaBite4031/Aurore/actions/workflows/ci.yml)
![C++23](https://img.shields.io/badge/C%2B%2B-23-5B7FFF?style=flat&logo=cplusplus&logoColor=white)
![Minecraft 1.21.11](https://img.shields.io/badge/Minecraft-1.21.11-65A765?style=flat)
![Protocol 774](https://img.shields.io/badge/Protocol-774-8B6FCF?style=flat)
![Status: First Light](https://img.shields.io/badge/Status-First%20Light-D88945?style=flat)
[![License: MIT](https://img.shields.io/badge/License-MIT-5D6B7A?style=flat)](LICENSE.txt)

**Windows x64 · Linux x86-64 · CMake-first**

*A ZettaTech Systems project.*

</div>

> [!IMPORTANT]
> Aurore is an **independent, unofficial** project. It is not affiliated with, endorsed by, sponsored by, or approved by Mojang Studios or Microsoft.

> [!WARNING]
> **First Light is a technical proof of concept, not a playable or production-ready Minecraft server.** You can connect and stand in a tiny world, but gameplay, persistence, and online account authentication are not implemented. Keep it on a trusted local network for now.

## Hello, world

Aurore is an original Minecraft-compatible server implementation written in modern C++. It doesn't wrap or embed the vanilla server. Instead, it builds the protocol, networking, runtime, and world model as independent systems, with tests to make sure those systems agree on what happens next.

I started Aurore because Minecraft is fascinating, C++ is fascinating, and understanding how a server works all the way down to its sockets seemed like a very good way to spend my time. What began as a protocol experiment is growing into a longer-term attempt at a fast, faithful, understandable Minecraft platform.

The current compatibility target is **Minecraft: Java Edition 1.21.11 (protocol 774)**. Other versions are not supported by First Light.

## First Light: what works today

**`v0.1.0-first-light`** is Aurore's first end-to-end milestone. An unmodified vanilla 1.21.11 client can connect through the normal protocol phases, enter Play, receive a small world, and remain connected while moving around.

```text
Handshake ──► Status
     │
     └──────► Login ──► Configuration ──► Play
                         registries        │
                         and tags           ├─ initial player/world state
                                            ├─ 3×3 chunk neighborhood
                                            ├─ position + movement input
                                            ├─ keepalives + timeouts
                                            └─ clean client lifecycle
```

Implemented for this milestone:

- Native, bounded network transport using **IOCP** on Windows and **epoll** on Linux.
- Handshake, server Status, **offline-mode Login**, and the Configuration exchange.
- Minecraft data preparation from official sources, integrity checks, cached generation, and validated registry/tag import.
- An immutable, generation-tracked registry snapshot shared consistently across a Configuration session.
- First Play bootstrap, player admission, position synchronization, and an initial chunk batch.
- A **5×5 grass platform** with dirt and stone underneath, inside a **3×3 transmitted chunk neighborhood**. Surrounding chunks are effectively empty.
- Player movement input, initial teleport acknowledgement, chunk-batch feedback, client-loaded notification, and server-issued keepalives.
- Explicit disconnect, timeout, shutdown, and resource-cleanup paths, backed by unit, integration, loopback, and stress tests.

The small world is intentional. First Light is about proving that the server can reach and maintain a real Play session, **not** about disguising an unfinished game as a complete one.

### What is *not* implemented

There is no meaningful survival or creative gameplay yet: no persistent world or terrain generation, no working block placement/breaking, no real inventory or crafting system, no mob gameplay, no commands/permissions, and no production multiplayer security. Unsupported client actions may be ignored or rejected, and some may disconnect the session. The implementation is currently designed for trusted local testing, not internet-facing public hosting.

**Alpha 1** will begin turning the proof of concept into an interactive world. See the [roadmap](docs/ROADMAP.md).

## Getting started

### Downloading a release

The First Light release is intended to offer three downloads:

| Archive | Contents |
|---|---|
| `Aurore-v0.1.0-first-light-windows-x64.zip` | Windows Release build and required runtime files |
| `Aurore-v0.1.0-first-light-linux-x86_64.zip` | Linux x86-64 Release build and required runtime files |
| `Aurore-v0.1.0-first-light-source.zip` | Complete source snapshot, including pinned source dependencies |

Check the [Releases](https://github.com/ZettaBite4031/Aurore/releases) page for published assets and any platform-specific runtime requirements. The packages are prepared and validated according to [Releasing](docs/RELEASING.md); an asset is not available until it has actually been uploaded.

### Building from source

Clone with the pinned Git submodules:

```bash
git clone --recurse-submodules https://github.com/ZettaBite4031/Aurore.git
cd Aurore
```

**Windows:** Windows x64, Visual Studio 2026 with the C++ workload, and CMake 3.25+.

```powershell
cmake --preset windows-release
cmake --build --preset windows-release --parallel
ctest --preset windows-release
```

**Linux:** Linux x86-64, CMake 3.25+, Ninja, a toolchain with Aurore's required C++23 library support, and libcurl development headers. On Debian/Ubuntu, install `libcurl4-openssl-dev` along with the regular C++ build tools.

```bash
cmake --preset linux-release
cmake --build --preset linux-release --parallel
ctest --preset linux-release
```

Debug presets and full setup instructions live in [Development](docs/DEVELOPMENT.md).

### Running the server

Run from the directory where you want Aurore to keep its `config/` and `cache/` folders. The default configuration is created at **`config/aurore.json` relative to the working directory**. It listens on port `25565`, bound to `0.0.0.0` by default. For local testing, prefer `127.0.0.1` as the bind address and connect your vanilla 1.21.11 client to `localhost`.

Windows build output:

```powershell
./out/build/windows-release/bin/Aurore.Server.exe
```

Linux build output:

```bash
./out/build/linux-release/bin/Aurore.Server
```

On its first startup, Aurore prepares Minecraft's required data from official sources. This may need network access and a compatible installed **Java runtime**. Subsequent startups can reuse a validated, content-addressed cache. Aurore itself is native C++; Java is used for the upstream data-generation step, not to run the server.

To use an existing generated corpus, set `AURORE_MINECRAFT_DATA_ROOT` to the **SHA-1-named generation directory** containing `reports/` and `data/`. Set `AURORE_JAVA_EXECUTABLE` to a specific Java executable when necessary. Neither the corpus nor Mojang's server artifact is included in Aurore's releases.

> [!NOTE]
> First Light implements offline-mode login for local protocol testing. It **does not verify ownership of Minecraft accounts**. Do not treat it as an authenticated public server.

## How Aurore is organized

| Project | Responsibility |
|---|---|
| `Aurore.Server` | Startup, production data preparation, and application composition |
| `Aurore.Core` | Runtime policy, player admission, connection lifecycle, and orchestration |
| `Aurore.Network` | Native socket backends, bounded queues, and transport lifecycle |
| `Aurore.Protocol` | Packet framing, codecs, states, registry synchronization, and Play messages |
| `Aurore.Data` | Official-source resolution, local generation, cache verification, and vanilla data import |
| `Aurore.World` | Dimensions, players, chunks, and ordered simulation phases |
| `Aurore.Util` | Reusable values, byte/NBT tools, registries, and immutable snapshots |
| `Aurore.DataTool` | Standalone data-preparation utility |
| `Aurore.Tests` | Tests spanning protocols, subsystems, real sockets, and failure paths |

The central rule is simple: **Network moves bytes, Protocol understands packets, Data prepares authoritative inputs, Core makes decisions, World owns simulation, and Server puts it all together.**

Aurore is written to keep observable order and state ownership understandable. Performance is a goal, but correctness and compatibility provide the baseline from which performance work can actually be measured. More detail: [Architecture](docs/ARCHITECTURE.md).

## Future vision

First Light is one tiny piece of a much larger idea. These are **directions to explore**, not implemented features or promises of a delivery date.

**Faithful Minecraft behavior.** The goal is to follow vanilla behavior closely enough that technical builds, subtle update ordering, and even compatibility-sensitive quirks can continue to work. A future server might offer explicit fidelity/performance modes for situations where different tradeoffs are acceptable, without silently changing behavior.

**Performance with evidence.** More efficient networking, world ticking, scheduling, memory ownership, and eventually profiling-driven parallelism should make Aurore fast for real workloads, not merely impressive in a synthetic benchmark.

**A platform, not just a process.** Extensible server systems, version-aware protocol boundaries, potential cross-version and Java/Bedrock interoperability, and long-term distributed-server ideas all deserve investigation once the basic game works. Each would need its own design and compatibility proof.

**An eventual Aurore client.** A separate client project might one day explore rendering, responsiveness, accessibility, and customization. It isn't part of this repository or the First Light release. No client assets or implementation are currently promised.

Aurore is a passion project. The long-term vision is ambitious because exploring interesting systems is the entire point. The [roadmap](docs/ROADMAP.md) distinguishes near-term milestones from these bigger questions.

## Contributing and contact

Questions, suggestions, interoperability reports, and carefully scoped contributions are welcome. Read [Contributing](CONTRIBUTING.md) and [Development](docs/DEVELOPMENT.md) first.

- **Discord:** `@mirathatnardo`
- **Telegram:** `@mirathatnardo`
- **Email:** *Not configured yet*

For reproducible bugs, [GitHub Issues](https://github.com/ZettaBite4031/Aurore/issues) are also useful. Please do not publish credentials or private connection details in an issue.

## Documentation

| Document | Subject |
|---|---|
| [Architecture](docs/ARCHITECTURE.md) | Ownership, boundaries, and server execution |
| [Development](docs/DEVELOPMENT.md) | Toolchains, building, testing, runtime setup |
| [Protocol and Data](docs/PROTOCOL_AND_DATA.md) | Wire contracts and local Minecraft data preparation |
| [Roadmap](docs/ROADMAP.md) | First Light, Alpha milestones, and longer-term goals |
| [Releasing](docs/RELEASING.md) | Source and binary packaging, verification, and tagging |
| [Legal and Provenance](docs/LEGAL_AND_PROVENANCE.md) | Independent implementation and distribution practices |
| [To Mojang](docs/TO_MOJANG.md) | A personal note about the project and an invitation to talk |
| [Third-Party Notices](THIRD_PARTY_NOTICES.md) | Pinned dependencies and licensing status |

## Independence and interoperability

Aurore is independently developed under the ZettaTech Systems project name. **Aurore and ZettaTech Systems are not affiliated with, sponsored by, endorsed by, or officially associated with Mojang Studios or Microsoft.** Minecraft and related names are mentioned only to explain compatibility.

Aurore's distributed project code is original; release packages must not bundle official Minecraft software, assets, or locally generated Minecraft datasets. Where the server needs upstream data, it retrieves and processes that material separately on the user's machine. For the project's approach and an open invitation to discuss concerns, see [Legal and Provenance](docs/LEGAL_AND_PROVENANCE.md) and [To Mojang](docs/TO_MOJANG.md).

## License

Aurore-authored code and documentation are licensed under [MIT](LICENSE.txt). Third-party dependencies and Minecraft-related materials remain subject to their respective licenses and terms.
