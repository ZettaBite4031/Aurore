<div align="center">

# Aurore

### A from-scratch Minecraft-compatible server, built as a systems project

**Exact state · Clear ownership · Bounded resources · One bright vertical slice at a time**

![C++](https://img.shields.io/badge/C%2B%2B-latest-00599C?style=flat&logo=cplusplus&logoColor=white)
![Platform](https://img.shields.io/badge/Platform-Windows%20x64-0078D4?style=flat&logo=windows11&logoColor=white)
![Protocol](https://img.shields.io/badge/Protocol-774-4C9A2A?style=flat)
![Status](https://img.shields.io/badge/Status-Pre--Alpha-D97706?style=flat)

*A ZTS systems project.*

</div>

> [!WARNING]
> Aurore is not playable yet. It is pre-alpha engineering work, not a production server.

> [!IMPORTANT]
> Aurore is an independent project. It is not an official Minecraft product and is not approved by or associated with Mojang Studios or Microsoft.

## What is Aurore?

Aurore is a passion-driven attempt to build a fast, understandable Minecraft-compatible platform in modern C++. The current repository is the server half of that idea: transport, protocol, client lifecycle, registry data, and world ownership are being assembled into one coherent vertical slice instead of a pile of disconnected features.

The long-range Aurore name covers both sides of the experience:

- **Aurore Server** — the server runtime in this repository.
- **Aurore Client** — a future custom client focused on performance, rendering, interface design, and modding.

They are related products, not one executable. The client is a future direction and has not been started here.

## Where it stands

The server currently has:

- a production Windows IOCP transport with bounded queues and resource accounting;
- Status, offline Login, and Configuration state handling for protocol 774;
- explicit Core-owned identity admission, lifecycle transitions, and timeouts;
- bounded packet, NBT, registry, tag, and snapshot infrastructure;
- immutable registry generations and compatibility validation;
- deterministic synthetic data for tests;
- unit, component, backend, and real loopback tests;
- Debug and Release Windows CI.

An unmodified 1.21.11 client can complete Handshake and offline Login and receive the Configuration sequence. It currently rejects Aurore's deliberately synthetic registry content before acknowledging Configuration. That is the active compatibility boundary.

No chunks, entities, persistence, gameplay, online authentication, compression, or public mod API exist yet.

## Shape of the codebase

| Project | Owns |
|---|---|
| `Aurore.Server` | Executable entry point and composition |
| `Aurore.Core` | Runtime policy, client orchestration, identity, and snapshot publication |
| `Aurore.Network` | Minecraft-agnostic byte transport and IOCP |
| `Aurore.Protocol` | Framing, codecs, protocol state, and Configuration sequencing |
| `Aurore.Util` | Byte primitives, UUIDs, NBT, registries, tags, and snapshots |
| `Aurore.World` | World ownership and explicit tick phases |
| `Aurore.Tests` | Unit, component, backend, and loopback verification |

The important dependency rule is simple: Network moves bytes, Protocol understands the wire, Core makes server decisions, and World owns simulation state.

## Build it

Requirements:

- Windows x64;
- Visual Studio 2026 with the MSVC v145 C++ workload;
- CMake and PowerShell;
- Git with submodule support.

```powershell
git clone --recurse-submodules https://github.com/ZettaBite4031/Aurore.git
cd Aurore
./scripts/build.ps1 -Configuration Debug
```

The script builds pinned dependencies, builds `Aurore.slnx`, copies the required Sonnet runtime, and runs the test suite. Use `Release` for the release configuration. See [Development](docs/DEVELOPMENT.md) for the full workflow and source-archive setup.

## The next horizon

The next product milestone is **First Light**: one vanilla client moves through Configuration, enters a deliberately tiny Play world, remains synchronized, and disconnects cleanly. The point is not broad gameplay. The point is a visible, testable end-to-end experience that proves every layer participates correctly.

See the [roadmap](docs/ROADMAP.md) for the boundary between the current cleanup baseline, First Light, and the future Aurore client.

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Development](docs/DEVELOPMENT.md)
- [Protocol and data](docs/PROTOCOL_AND_DATA.md)
- [Roadmap](docs/ROADMAP.md)
- [Legal and provenance policy](docs/LEGAL_AND_PROVENANCE.md)
- [Contributing](CONTRIBUTING.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

## License

Aurore-authored source and documentation are available under the [MIT License](LICENSE.txt). Dependencies and third-party names remain subject to their own terms.
