# Contributing to Aurore

Aurore is experimental, but its boundaries are deliberate. Contributions are welcome when they make the system clearer, safer, or more complete without blurring ownership and execution order.

## Before writing code

Read [Architecture](docs/ARCHITECTURE.md), [Development](docs/DEVELOPMENT.md), and the section of [Protocol and data](docs/PROTOCOL_AND_DATA.md) relevant to the change. Changes involving third-party data or compatibility research must also follow [Legal and provenance policy](docs/LEGAL_AND_PROVENANCE.md).

## What a good change looks like

- One subsystem clearly owns the behavior.
- Network remains a byte transport with no Minecraft semantics.
- Protocol owns wire representation and state validation; Core owns runtime policy.
- Untrusted sizes, recursion, retained memory, and queued work remain bounded.
- State is validated and constructed before it becomes authoritative.
- Ordering is visible in code rather than hidden behind a general event abstraction.
- Tests exercise the highest practical layer, including failure paths.
- Documentation changes when a public contract, boundary, or milestone changes.

## Style

Aurore favors direct modern C++ over ornamental abstraction:

- PascalCase for types and functions;
- `m_` prefixes for member state;
- tabs in C++ source;
- explicit ownership and move-only values where ownership transfers;
- small cohesive groups of files, not one file per trivial type;
- comments for invariants and non-obvious reasons, not line-by-line narration;
- virtual dispatch only where runtime substitution is part of the design.

Match the surrounding code. A focused, readable implementation is more valuable than a generic framework built in anticipation of possible use.

## Build and test

Run both supported configurations before requesting review:

```powershell
./scripts/build.ps1 -Configuration Debug
./scripts/build.ps1 -Configuration Release
```

If dependencies are already built, `-SkipDependencies` avoids rebuilding them. `-SkipTests` exists for local iteration, but review-ready changes should run the full suite.

## Clean implementation boundary

Do not contribute copied, decompiled, mechanically translated, or proprietary game implementation material. Do not commit official software, assets, complete extracted registries, local generated caches, credentials, or data with unclear redistribution rights.

Use original fixtures under an Aurore-owned namespace such as `aurore_test`. Record compatibility behavior as independently observed inputs, outputs, ordering, limits, and failure behavior.

## Scope discipline

For roadmap work, prefer a narrow vertical slice with a visible exit condition. Refactoring should preserve behavior and arrive separately from new protocol or gameplay capability whenever practical.
