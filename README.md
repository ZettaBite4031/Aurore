<div align="center">

# Aurore

### A from-scratch Minecraft-compatible server built as a systems project

**Explicit ownership · Bounded resources · Deterministic state · Vertical slices**

[![Build and Test](https://github.com/ZettaBite4031/Aurore/actions/workflows/ci.yml/badge.svg)](https://github.com/ZettaBite4031/Aurore/actions/workflows/ci.yml)
![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?style=flat&logo=cplusplus&logoColor=white)
![Protocol 774](https://img.shields.io/badge/Minecraft%20Protocol-774-4C9A2A?style=flat)
![Status](https://img.shields.io/badge/Status-Pre--Alpha-D97706?style=flat)

**Windows x64 runtime · Linux x86-64 build target · CMake-first**

*A ZettaTech Systems project.*

</div>

> [!WARNING]
> Aurore is not playable yet. It is pre-alpha engineering work, not a production Minecraft server.

> [!IMPORTANT]
> Aurore is an independent project. It is not an official Minecraft product and is not approved by or associated with Mojang Studios or Microsoft.

---

## What is Aurore?

Aurore is an attempt to build a fast, understandable Minecraft-compatible platform in modern C++ without hiding the interesting systems work behind a large framework.

The server is being built as a sequence of narrow vertical slices. Networking, protocol state, client lifecycle, registry data, world ownership, and eventually gameplay are expected to meet at tested boundaries rather than grow as disconnected feature islands.

The long-range Aurore name covers two related products:

| Product | Direction |
|---|---|
| **Aurore Server** | The server runtime in this repository |
| **Aurore Client** | A future custom client focused on performance, rendering, interface design, accessibility, and modding |

They share a product identity, not a runtime or executable. The client has not been started.

## Current signal

```text
Build system        CMake
Language            C++23
Protocol target     774 / Minecraft 1.21.11

Windows x64
  Build             ✓ Debug / Release
  Tests             ✓ Debug / Release
  Runtime backend   ✓ IOCP

Linux x86-64
  Build             ✓ Debug / Release
  Tests             ✓ Debug / Release
  Runtime backend   · epoll pending
```

Aurore currently provides:

- a production Windows IOCP transport with bounded command/event queues and explicit resource accounting;
- a platform-neutral network manager and backend contract prepared for additional native transports;
- Status, offline Login, and Configuration handling for protocol 774;
- Core-owned identity admission, client lifecycle transitions, and timeouts;
- bounded packet, NBT, registry, tag, and snapshot infrastructure;
- immutable registry generations and compatibility validation;
- deterministic synthetic data for automated tests;
- unit, component, protocol, network-contract, backend, and Windows loopback coverage;
- four-way CI across Windows/Linux and Debug/Release.

An unmodified Minecraft 1.21.11 client can complete Handshake and offline Login and receive Aurore's Configuration sequence. The current compatibility boundary is registry data: the deliberately synthetic bootstrap snapshot is not a complete vanilla registry set, so the client does not yet complete Configuration.

No chunks, entities, persistence, gameplay, online authentication, compression, or public mod API exist yet.

## The shape of the system

Aurore is organized around ownership.

```text
                       ┌─────────────────┐
                       │  Aurore.Server  │
                       │ composition     │
                       └────────┬────────┘
                                │
                       ┌────────▼────────┐
                       │   Aurore.Core   │
                       │ runtime policy  │
                       └───┬────────┬────┘
                           │        │
                  ┌────────▼───┐ ┌──▼────────────┐
                  │ Protocol   │ │ World         │
                  │ wire/state │ │ simulation    │
                  └──────┬─────┘ └───────────────┘
                         │
                  ┌──────▼──────┐
                  │ Network     │
                  │ byte I/O    │
                  └──────┬──────┘
                         │
                  ┌──────▼──────┐
                  │ native OS   │
                  │ backend     │
                  └─────────────┘

             Util supports the reusable value/data layer.
```

| Project | Owns |
|---|---|
| `Aurore.Server` | Process entry point and top-level composition |
| `Aurore.Core` | Runtime policy, client orchestration, identity, and snapshot publication |
| `Aurore.Network` | Minecraft-agnostic byte transport, queues, limits, and backend lifecycle |
| `Aurore.Protocol` | Framing, codecs, protocol state, requests, and Configuration sequencing |
| `Aurore.Util` | Byte primitives, UUIDs, NBT, registries, tags, and snapshots |
| `Aurore.World` | World ownership and explicit tick phases |
| `Aurore.Tests` | Unit, component, contract, backend, and loopback verification |

The dependency rule is intentionally plain:

> **Network moves bytes. Protocol understands the wire. Core makes server decisions. World owns simulation state.**

More detail lives in [Architecture](docs/ARCHITECTURE.md).

## Build it

Aurore uses **CMake as the canonical build system**. Dependencies are pinned as Git submodules and participate directly in the CMake graph.

Clone with submodules:

```bash
git clone --recurse-submodules https://github.com/ZettaBite4031/Aurore.git
cd Aurore
```

### Linux

Requirements:

- x86-64 Linux;
- CMake 3.25 or newer;
- Ninja;
- a C++23-capable GCC or Clang toolchain;
- Git.

Debug:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug --parallel
ctest --preset linux-debug
```

Or use the convenience wrapper:

```bash
./scripts/build.sh Debug
```

Release:

```bash
./scripts/build.sh Release
```

> [!NOTE]
> Linux is currently a build-and-test target, not yet a functional server runtime. The native epoll network backend is the next portability milestone.

### Windows

Requirements:

- Windows x64;
- Visual Studio 2026 with the MSVC v145 C++ workload;
- CMake;
- PowerShell;
- Git.

Debug:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel
ctest --preset windows-debug
```

Or:

```powershell
./scripts/build.ps1 -Configuration Debug
```

Use `Release` for the release configuration.

See [Development](docs/DEVELOPMENT.md) for the complete build, test, preset, dependency, and contribution workflow.

## The next horizon

The product milestone remains **First Light**:

> One unmodified vanilla client completes Configuration, enters a deliberately tiny Play context, remains synchronized, and disconnects cleanly.

Before returning to that path, Aurore is completing its Linux runtime portability baseline by implementing an epoll backend with the same observable transport contract as the existing IOCP backend.

After that, work returns to complete protocol-774 registry data, Configuration convergence, and the minimum Play bootstrap.

See the [Roadmap](docs/ROADMAP.md) for the broader milestone boundary.

## Engineering character

Aurore deliberately favors:

- explicit ownership over shared mutable state;
- typed subsystem boundaries over implicit coupling;
- deterministic state publication over partially visible mutation;
- bounded queues and retained memory;
- visible execution order;
- testable behavior over speculative abstraction;
- a real second implementation before generalizing an interface.

It is a systems project first. Performance matters, but predictable behavior and understandable ownership come before cleverness.

## Documentation

- [Architecture](docs/ARCHITECTURE.md) — subsystem ownership and runtime flow
- [Development](docs/DEVELOPMENT.md) — building, testing, dependencies, and workflow
- [Protocol and data](docs/PROTOCOL_AND_DATA.md) — protocol/data contracts and compatibility work
- [Roadmap](docs/ROADMAP.md) — vertical slices and First Light
- [Legal and provenance policy](docs/LEGAL_AND_PROVENANCE.md) — clean implementation boundaries
- [Contributing](CONTRIBUTING.md) — contribution expectations
- [Third-party notices](THIRD_PARTY_NOTICES.md) — dependency provenance and pins

## License

Aurore-authored source and documentation are available under the [MIT License](LICENSE.txt). Dependencies and third-party names remain subject to their own terms.
