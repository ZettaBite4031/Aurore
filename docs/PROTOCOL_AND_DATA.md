# Protocol and Data

## Target and current evidence

The current compatibility target is Minecraft: Java Edition 1.21.11, protocol 774.

An unmodified client has been observed to:

1. complete Handshake;
2. complete offline Login;
3. accept Feature Flags and participate in Known Packs selection;
4. decode Registry Data and Update Tags envelopes;
5. receive clientbound Finish Configuration;
6. reject the incomplete synthetic registry dataset before sending serverbound Finish Configuration.

The demonstrated failure is registry-content compatibility, not a claim that all framing or sequencing is universally complete.

## Protocol states

The implemented state paths are:

```text
Handshake -> Status -> Disconnected
Handshake -> Login -> Configuration -> internal Play admission
```

Protocol owns packet IDs, direction, decoding, framing, transformations, state validation, and typed requests/resolutions. It does not decide whether an identity should be admitted or perform network I/O.

State changes are transactional: validation and required output construction complete before authoritative state advances. Invalid packets, stale request IDs, invalid Known Packs selections, and generation mismatches fail explicitly.

## Configuration sequence

The present sequence supports Feature Flags, Known Packs, Registry Data, Update Tags, and Finish Configuration. A move-only transmission plan retains the registry generation used to build the exchange.

The compatibility model includes:

- a protocol-774 registry manifest;
- typed and generic registry representations;
- required, optional, and forbidden entry-data and tag policies;
- dependency and minimum-count validation;
- an adapter from immutable snapshots to manifest-facing inventory.

The remaining compatibility work is to make post-negotiation materialization selection-aware and manifest-driven, then supply a complete provenance-safe dataset. That belongs to the next implementation phase, not this cleanup baseline.

## Data structures

`RegistrySnapshot` combines typed registries, typed tags, generic network registries, and generic tag sections under one immutable generation. Generic entries preserve runtime-ID order and optional NBT payload presence. Tag members resolve to generation-local runtime IDs during construction.

Parsers and builders bound packet sizes, strings, arrays, NBT size and depth, registry counts, tag sections, tag members, and retained output. Incomplete transactional reads do not advance the caller-visible buffer position.

## Synthetic versus production data

The checked-in synthetic generation exists to exercise ownership, encoding, ordering, validation, and lifetime rules. It intentionally does not reproduce a complete vanilla registry dataset and must not be described as production data.

Future production data should use a versioned local cache containing source identity, hashes, target version, generator and schema versions, ordered registries, tags, and validation results. The loader must reject incompatible or ambiguous input and publish only a fully validated snapshot.

The repository and release artifacts must not contain official software, assets, or generated datasets whose redistribution rights have not been established. See [Legal and provenance policy](LEGAL_AND_PROVENANCE.md).

## Interoperability iteration

For each manual compatibility run, record the Aurore revision, client version and protocol, data/generator identity, selected packs, last successful packet, first failure, and whether Configuration acknowledgement and Core Play admission occurred.

Fix the earliest demonstrated incompatibility, encode it in an automated regression test, and rerun. Avoid broad speculative changes based on later cascade errors.
