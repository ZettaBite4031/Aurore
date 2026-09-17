# Roadmap

Aurore is developed as vertical slices. A milestone is complete when a user-visible path works end to end and the underlying boundaries are tested—not when many disconnected systems exist in isolation.

## Current baseline: clean foundations

The cleanup baseline establishes:

- focused server and client-orchestration ownership;
- protocol-owned diagnostics;
- subsystem-focused test files;
- one local/CI build path;
- an accurate sample configuration;
- a compact public documentation set.

This phase intentionally adds no gameplay or new protocol reach.

## First Light: the playable proof

The next implementation goal is one small but genuine experience:

- an unmodified client completes Configuration;
- Core admits the retained registry generation into Play;
- the server sends only the required Play bootstrap;
- one player has explicit ownership in one tiny world context;
- position, keep-alive, and disconnect behavior remain stable;
- the path has automated coverage wherever proprietary client software is not required.

Broad chunks, entities, inventories, commands, generation, and persistence are outside this slice unless the minimum client path proves one is mandatory.

## Work leading into First Light

1. Defer post-negotiation Configuration materialization until Known Packs selection.
2. Drive Registry Data and Update Tags from the compatibility manifest.
3. Define a deterministic, provenance-recorded local data cache.
4. Load and validate a complete protocol-774 registry snapshot.
5. Iterate against the earliest observed vanilla-client failure.
6. Implement the minimum Play bootstrap only after Configuration completes.

## After First Light

Likely server work includes world storage and generation, block state, entities and tracking, inventory and containers, commands and permissions, save transactions, compression, batching, profiling, and online authentication. Exact ordering should follow measured vertical-slice value rather than this list.

A public extension ABI should wait until ownership and compatibility contracts have survived real use. Early modding experiments may use internal APIs without promising stability.

## The future Aurore client

Aurore's long-range scope includes a separate custom client under the same brand. Its reasons to exist are different from the server's: rendering quality, performance, interface design, accessibility, and a cohesive modding experience.

The client should become its own repository and product boundary when the server has a credible end-to-end experience. Shared brand, protocol fixtures, and design language do not imply shared runtime code or release cadence.

The first client milestone should connect to something worth seeing. Until then, server First Light remains the recognition-building priority.

## Success discipline

Every milestone needs a written entry condition, observable exit condition, failure budget, and explicit list of deferred work. Cleanup, capability, and optimization should remain distinct enough that regressions can be traced and reviewed.
