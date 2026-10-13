# Roadmap

Aurore is built in **small, observable vertical slices**. Each milestone has a reason to exist, a concrete way to tell whether it works, and a deliberate list of things it *doesn't* try to accomplish.

The intent isn't to rush from zero to a complete Minecraft clone in one leap. It's to make the foundation dependable enough that the next feature has somewhere solid to land.

## First Light: `v0.1.0-first-light`

**Status: implementation and validation complete; release preparation in progress.**

First Light answers the question: *Can an independently implemented C++ server put an unmodified Minecraft Java 1.21.11 client into a tiny world and maintain a healthy Play session?*

Acceptance evidence:

- Handshake, Status, offline Login, and complete required Configuration negotiation.
- Locally generated and imported real vanilla registries/tags.
- First Play bootstrap, player/world admission, nine-column lit chunk transfer.
- A fixed platform, movement input, position sync, keepalive, and session cleanup.
- Bounded network resources and timeout/failure-path handling.
- More than 550 automated tests reported passing, plus Debug/Release validation on Windows and Linux and a vanilla-client smoke test.

First Light is **not** a release of playable Minecraft survival/creative gameplay, procedural worlds, persistent storage, or online authentication. The initial release is labeled a prerelease for that reason.

## Next: Alpha 1, the first interactive world

**Status: planned; scope is intentionally provisional.**

The next meaningful milestone is an actually interactive, deliberately small world. A sensible initial slice would include:

1. An authoritative block state model connecting player interactions to World state.
2. Breaking and placing blocks through the relevant Play packets and confirmations.
3. Correct world-to-client block update propagation.
4. A simple inventory/held-item contract sufficient for those interactions.
5. Player movement and interaction tests across disconnects, resets, and concurrent clients.

We should define Alpha 1's exact exit criteria before implementing it, rather than promise these details as a fixed release checklist today. Persistence, general terrain generation, and most survival systems need not arrive together with the first editable world.

## Beyond Alpha 1

Likely future work includes, in an order determined by compatibility needs:

- More complete Play packet support and reliable chunk streaming.
- Persistent world storage and save integrity.
- Block updates, scheduled ticks, fluids, block entities, entities, and inventories.
- Commands, permissions, authenticated public-server operation, and administration.
- Terrain generation and richer simulation.
- Observability, profiling, memory/performance work, and regression benchmarks.
- Extension/modding interfaces once their ownership and compatibility contracts stabilize.

Aurore is intended to preserve vanilla behavior, including timing and ordering quirks that technical players depend on. Performance improvements should be benchmarked, tested, and documented. Explicit compatibility/performance profiles are a possible future design, not a shipped setting.

## The wider vision

There are bigger ideas worth exploring once the fundamentals work:

**Multiple versions and editions.** Version-specific protocol contracts might eventually support more Java versions and potentially Bedrock clients. This is substantially more complex than changing a protocol number, and it's not implemented today.

**High-performance simulation.** Strong ownership boundaries should make selective batching, reduced allocations, better work scheduling, and eventually safe multithreading possible. Preserve observable behavior unless the user explicitly selects a documented alternative.

**Distributed server architecture.** Multiple cooperating instances, movement between servers, shared world services, and other approaches to scale are long-term research questions. First Light is one server process.

**An independent Aurore client.** A separate project could eventually explore rendering, UI, accessibility, performance, and modding. It would need its own protocol, asset, and distribution review. The server must become worth connecting to first.

Nothing here promises a release date, an API commitment, or guaranteed compatibility. Big ideas are welcome; shipping them is a separate engineering decision.

## How we decide what to build

Each milestone should explain its entry state, implemented behavior, observable success criteria, test strategy, and deliberately deferred features. Small targeted refactors are encouraged when they unblock a concrete path. Unmeasured hyperoptimization and speculative frameworks can wait.

The first milestone is proof that Aurore can hold a connection to a real Minecraft world. The next milestone starts making that world respond.
