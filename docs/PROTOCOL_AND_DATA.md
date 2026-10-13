# Protocol and Data

Aurore's first real Minecraft client connection depends on two separate things going right: the **wire protocol** must be correct, and the **registry data** sent over it must be complete and consistent. First Light proves the necessary path for one specific client version; it doesn't claim implementation of every packet or rule.

## Supported target and evidence

| Field | First Light |
|---|---|
| Edition | Minecraft: Java Edition |
| Version | 1.21.11 |
| Network protocol | 774 |
| Login authentication | Offline-mode development implementation |
| Reaches Play with vanilla client | Yes, tested manually |
| World | Fixed First Light platform and surrounding empty chunk columns |
| Other versions / Bedrock | Not supported |

Compatibility is **version-specific**. Protocol IDs, ordering, registry schemas, and feature negotiation must be reviewed again for each future target version. Aurore is not yet a multi-version proxy or Bedrock bridge.

## Protocol state flow

```text
Handshake
├── Status (status query / ping, then closure)
└── Login (offline identity)
    └── Configuration
        ├── client information and optional brand
        ├── feature flags
        ├── known-packs negotiation
        ├── registry data and update tags
        ├── clientbound Finish Configuration
        └── serverbound Finish Configuration
            └── Play
                ├── player admission / initial world state
                ├── position + initial teleport acknowledgement
                ├── 3×3 chunk batch and feedback
                ├── client world-loaded notification
                ├── movement and selected safe input packets
                └── keepalive + disconnection lifecycle
```

The protocol layer owns decoding, encoding, framing, protocol-state validity, and typed requests or resolutions. Core owns policy, identity/player admission, timeouts, and World changes. Network only transports bytes.

Transitions are validated before authoritative state is committed. Malformed input and invalid state transitions fail explicitly and must not corrupt other sessions.

## Configuration contracts

Aurore negotiates Feature Flags and Known Packs. Registry Data and Update Tags are derived from a **single retained `RegistrySnapshot` generation**, and protocol-774 compatibility rules determine expected sections and payload policies.

The First Light importer currently handles all **23 synchronized registries** required by the Configuration contract, as well as supporting static identity registries and tag domains. Typed block/item registries, dimension types, biomes, generic synchronized entries, nested tags, optional references, required-reference validation, cycle detection, runtime-ID order, and immutable publication are exercised by tests.

Configuration can proceed to Play only after the required exchange and acknowledgements complete. There is no blanket claim that optional or future configuration packets are implemented.

## First Light Play behavior

Core's Play bootstrap creates a player with a fixed spawn and sends the data needed to render a tiny Overworld platform. The World object stores one center chunk with **75 platform blocks** across three layers. Protocol serializes that center and eight neighboring mostly empty columns, including lighting data, because the vanilla client needs the surrounding columns to finish terrain readiness.

The Play intake understands and handles the First Light subset: movement/rotation updates, acknowledgements for the initial teleport and chunk batch, the client-loaded notification, server-issued keepalive replies, client information, and selected input/interaction packets. Some interactions are deliberately parsed and deferred or acknowledged without changing world/inventory state. Others may be rejected. This is **not a general Play implementation**.

Player state, KeepAliveTracker state, synchronization deadlines, and transport ownership must be cleaned up when a session closes. Incorrect acknowledgements are handled according to their specific contracts rather than silently treating every unexpected value as success.

## Official-source data pipeline

Normal startup uses `ProductionBootstrap` to prepare the source for the 1.21.11 registry snapshot:

```text
Mojang version metadata
      ↓
official server-artifact identity and download
      ↓
SHA-1 verification
      ↓
Java-driven local report/data generation
      ↓
content-addressed generated-data cache
      ↓
corpus structure and identity validation
      ↓
VanillaDataImporter / normalized registries and tags
      ↓
immutable RegistrySnapshot
      ↓
Core::Server startup and Configuration
```

This work happens on the user's machine, before the runtime accepts new clients. Aurore **does not ship Mojang's server JAR, generated registry dump, datapack files, textures, or other game assets** in its repository or planned release packages.

The generated corpus is keyed by the official server artifact's SHA-1. Existing complete generations are validated and reused; invalid or incomplete generations must not become authoritative. An alternative source path is available with `AURORE_MINECRAFT_DATA_ROOT`, pointing at the SHA-1 directory containing `reports/` and `data/`.

`AURORE_JAVA_EXECUTABLE` selects a particular Java executable for the upstream generation step. It is not used to run the Aurore C++ server. The source, licensing, and redistribution boundaries are documented in [Legal and Provenance](LEGAL_AND_PROVENANCE.md).

## Synthetic fixtures versus production data

Synthetic registry snapshots are kept for deterministic tests and narrow failure cases. They are intentionally incomplete as vanilla data and are **not** the production server startup input. Production data uses the official-source pipeline and normalized import.

Tests should use original, synthetic fixture values and avoid committing extracted Mojang data. Some higher-level tests can use a locally prepared corpus via explicit opt-in rather than making CI depend on downloaded Minecraft artifacts.

## Limits and failure behavior

Aurore bounds untrusted frame and packet sizes, array/string limits, NBT depth and bytes, registry/tag counts, network queues, retained events, connection counts, and deadlines. Configuration plans and snapshots are validated before publication. Receives can be partial and should remain valid until a full frame is available, within their respective caps.

These limits are correctness and availability safeguards, not proof of public-server security. First Light has no online account verification, gameplay permissions, or production abuse protections.

## Compatibility regression workflow

For a client-side failure, record:

1. Exact client edition/version and Aurore commit or tag.
2. Relevant source data identity and whether a validated cache was reused.
3. Last successfully completed protocol state and packet exchange.
4. First observed error or disconnect, with logs stripped of private information.
5. The smallest automated test that reproduces the failure without requiring proprietary files in the repository.

Fix the earliest demonstrated incompatibility, verify the regression, then repeat the real-client test. Avoid introducing broad workarounds for later cascade symptoms.

For runtime architecture, see [Architecture](ARCHITECTURE.md). For data provenance and distribution boundaries, see [Legal and Provenance](LEGAL_AND_PROVENANCE.md).
