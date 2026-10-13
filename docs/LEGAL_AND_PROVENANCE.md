# Legal and Provenance

Aurore is an independent Minecraft-compatible software project. This document explains **how we intend to build and distribute it responsibly**, not why anyone should or shouldn't bring a legal claim. It's a practical development policy, not legal advice.

> [!IMPORTANT]
> Aurore and ZettaTech Systems are not affiliated with, approved by, sponsored by, or endorsed by Mojang Studios or Microsoft. Minecraft-related names are used descriptively to explain compatibility.

## Original implementation

Aurore's source code is written independently. We work from published protocol information, lawful observation and interoperability testing, our own implementation notes, and original fixtures. We don't want copied or mechanically translated Minecraft implementation code in the project.

Our intent is to learn, experiment with server architecture and performance, and build original software that communicates with Minecraft clients. Aurore is not intended to replace a purchased copy of Minecraft or offer unauthorized access to the game.

The independent nature of the project is important to us, but **independence is not an exemption from applicable rights or terms**. We should keep the relevant sources and boundaries visible rather than make assumptions about blanket permissions.

## What we distribute

The public Aurore repository and release packages may contain:

- Original Aurore source, documentation, configuration examples, and synthetic test fixtures.
- Binaries built from that source, once their own redistribution dependencies have been checked.
- Appropriately licensed third-party dependencies and their required notices.
- Small implementation-independent compatibility facts needed by the software.

They should **not** contain:

- Mojang-authored Minecraft client/server binaries, official assets, or copied implementation source.
- Complete extracted Minecraft registry data, generated reports, datapacks, or local data caches by default.
- Unreviewed third-party source or materials with unclear redistribution permission.
- Credentials, account/session tokens, private captures, identifying logs, or secrets.

The fact that information was generated locally doesn't by itself give us permission to redistribute it. Local caches belong outside the repository and its release assets.

## Official-source data acquired locally

The First Light server needs registry and tag data corresponding to Minecraft: Java Edition 1.21.11. Rather than ship a copy of that data, Aurore:

1. Resolves official Minecraft version metadata and the official server artifact on the **user's machine**.
2. Downloads the artifact locally when necessary and verifies its source identity and SHA-1.
3. Uses a compatible Java runtime to run the required upstream data-generation step locally.
4. Validates the resulting corpus, stores it in a content-addressed cache, and imports normalized runtime models.
5. Reuses a previously validated generation when possible.

This acquisition is part of the existing production startup behavior. Alternatively, users may point Aurore to an already prepared corpus with `AURORE_MINECRAFT_DATA_ROOT`. No official artifact or generated corpus is included in Aurore's distributed source or binary archives.

Access to an official download endpoint is **not** a blanket grant of redistribution or other rights. The applicable Minecraft EULA, Usage Guidelines, and download terms still matter to users and to the project. We will revisit this workflow if those terms change or a legitimate concern is raised. Official materials and official-download processing remain separate from Aurore's MIT-licensed code.

## Authentication and First Light's scope

First Light currently implements **offline-mode Login** for development and local compatibility testing. It does not check ownership of a Minecraft account and should **not** be advertised or deployed as an authenticated public server. The project's aim is not to facilitate access without a legitimate copy of Minecraft.

A future public-hosting milestone should assess and implement appropriate official authentication, permission management, and related policy requirements before making the server available to untrusted users. The existence of an offline implementation is not a guarantee that offline hosting satisfies any particular platform rule or agreement.

## Dependencies and licenses

Aurore-authored material uses the [MIT License](../LICENSE.txt). Dependencies have their own ownership and license requirements.

- Sonnet is a separately maintained JSON library. Its developer intends to publish it under an attribution-preserving permissive open-source license, preferably MIT. The pinned revision reviewed for First Light did **not** contain a visible license file. **Don't treat that intention as a completed license grant.** Resolve the actual license in Sonnet's repository, update Aurore's pin, and record the resulting revision before binary publication.
- GoogleTest is a test-only dependency and is licensed under BSD-3-Clause at the reviewed revision. The exact source pin is recorded in [Third-Party Notices](../THIRD_PARTY_NOTICES.md).
- Binary packages may also depend on system libraries and compiler runtimes. Verify platform redistribution requirements and dynamic dependencies before publishing.

Updating a dependency means updating both its Git submodule revision and its notice, not just changing a number in a document.

## Contact and cooperation

The intention is to be transparent and approachable. If Mojang Studios, Microsoft, a contributor, or another rights holder has a concrete concern about some part of Aurore, we'd welcome a respectful conversation, examine the details, and explore reasonable corrections. We won't make advance promises about the outcome of a conversation we haven't had.

A personal introduction for Mojang is available in [To Mojang](TO_MOJANG.md).

Contact:

- Discord: `@mirathatnardo`
- Telegram: `@mirathatnardo`
- Email: *Not configured yet*

These are informal contact routes, not a formal legal-service address. Formal notices should use an appropriate available channel; when a dedicated project email is established, this document can be updated.

## Release review

Before publishing each new release:

- Confirm that source and packages contain only the intended distributable files.
- Review new dependencies, license obligations, and third-party notices.
- Ensure official game data is not in the assets or source archives.
- State supported versions, authentication behavior, and known limitations honestly.
- Revisit relevant upstream terms when the download model, online access, client work, or distribution model changes.
- Keep a clear path for corrections or legitimate concerns.

See [Releasing](RELEASING.md) for the concrete packaging checklist. This policy is meant to make it easier to do the right thing while continuing to work on a project we care about.
