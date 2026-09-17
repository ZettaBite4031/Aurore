# Architecture

Aurore is organized around ownership rather than feature categories. Each layer has a narrow reason to change, and cross-layer work moves through typed boundaries.

## Dependency direction

| Layer | Responsibility | Must not own |
|---|---|---|
| Server | Process entry point and top-level composition | Protocol behavior or transport details |
| Core | Runtime policy, client lifecycle, identity admission, orchestration | Packet encoding or socket operations |
| World | World and dimension ownership, ordered simulation phases | Client transport or packet framing |
| Protocol | Frames, codecs, state machines, requests, and resolutions | Admission policy or sockets |
| Network | Connections, byte I/O, queues, limits, and backend lifecycle | Packet IDs, NBT, identities, or worlds |
| Util | Cohesive reusable values and serialization primitives | Server policy |

`Aurore.Tests` may reach internal test-access boundaries where production encapsulation would otherwise make important lifecycle or backend invariants impossible to verify.

## Runtime flow

1. `NetworkManager` publishes connection, byte, close, and failure events from the selected backend.
2. `ClientService` drains those events on the server thread and owns the `ConnectionManager`.
3. Each `ClientConnection` passes bytes into a `ProtocolConnection`.
4. Protocol returns frames, typed requests, disposition, and structured failure context.
5. Core applies server policy, resolves requests, and queues complete outbound frames.
6. `Server` advances explicit tick phases and asks `WorldManager` and `ClientService` to do their work.

The server thread remains the authority for client and world state. IOCP workers own platform I/O, not game policy.

## Important ownership boundaries

### Client orchestration

`ClientService` owns the connected-client collection, login policy application, Configuration request handling, timeouts, and shutdown transitions. `Server` owns initialization order and the tick loop. This keeps network event plumbing out of the top-level process object without inventing a general service framework.

### Protocol diagnostics

Human-readable names for protocol states, stages, and errors live in `Aurore.Protocol`. Core logging consumes those functions rather than maintaining a second interpretation of protocol enums.

### Registry generations

Registry content is built as a complete candidate `RegistrySnapshot`, validated, then published through `RegistrySnapshotStore`. Readers retain immutable generations. A partially built registry collection never becomes active state.

### Network resources

Inbound and outbound bytes, commands, events, and connections have explicit limits. Receive credit remains reserved until Core drains the corresponding event. Graceful close preserves queued-send order; immediate close discards work according to the backend contract.

## Design rules

- Prefer a concrete owner over shared mutable state.
- Make ordering visible at the call site.
- Validate before committing state or consuming a single-use plan.
- Use typed errors at subsystem boundaries and log where useful context exists.
- Keep public surface area smaller than internal implementation detail.
- Add abstraction after a real second implementation or substitution need appears.
- Keep cleanup and new capability separable enough to review independently.

## Current deliberate placeholders

`WorldManager` and `Dimension` expose ordered tick phases even though most gameplay work is absent. Several top-level server phases are also empty. They document intended observable ordering but are not evidence of implemented gameplay. These placeholders should gain behavior only as part of a tested vertical slice; they should not accumulate speculative frameworks.
