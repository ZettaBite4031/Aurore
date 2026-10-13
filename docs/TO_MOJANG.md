# To Mojang Studios

Hello, Mojang!

I'm the developer behind **Aurore**, an independent, open-source Minecraft-compatible server written in C++23. I wanted to leave this note here in case someone from Mojang Studios or Microsoft ever comes across the project and is curious about what it is, why it exists, or how I'm approaching Minecraft interoperability.

## Why I'm making Aurore

I love Minecraft, and I love programming. Aurore started because I wanted to understand the mechanics of a Minecraft server from the network protocol upward: how a client connects, how data is synchronized, how a world is represented, and how all of that can be built into software that is both fast and understandable.

This isn't an attempt to publish another copy of Minecraft or replace your work. It's a personal engineering passion project, shared openly so other curious developers can learn from it, discuss it, and perhaps contribute.

The current milestone, **First Light**, is deliberately tiny: a vanilla Java Edition client can connect, receive a small fixed platform, move around, and remain synchronized. Most gameplay doesn't exist yet. The longer-term ambition is to explore a faithful and efficient Minecraft-compatible server, but the small working steps matter more than making big promises.

## How I'm approaching compatibility

Aurore is an independent implementation, not a modified or embedded copy of your Java server. The repository and planned release archives do not distribute Minecraft server/client binaries, game assets, or extracted game-data corpora.

Where Aurore needs version-specific registry data, the server acquires the required official artifact separately on the user's computer, verifies it, generates the data locally, and caches the result there. That data isn't packaged with Aurore. I want to respect your intellectual property and be clear about the boundary between your materials and my code.

First Light uses an offline-login implementation for local protocol development, not to offer a service that bypasses legitimate Minecraft ownership. It's a limited proof of concept, not an authenticated public server.

The technical details and distribution practices are described in [Protocol and Data](PROTOCOL_AND_DATA.md) and [Legal and Provenance](LEGAL_AND_PROVENANCE.md).

## If something concerns you

I don't know everyone at Mojang, and I don't want to guess what concerns you might or might not have. If some implementation detail, data workflow, feature, or release practice raises an issue, I'd genuinely appreciate hearing about it.

I'm happy to discuss a specific concern, understand the reasoning behind it, and explore reasonable changes that address it while keeping Aurore's independent engineering goals intact. I'd much rather have a constructive conversation than turn a project born from an appreciation for Minecraft into an unnecessary argument.

This note is an introduction and an invitation to communicate, not a request for endorsement, permission by silence, or a statement of legal conclusions.

## Getting in touch

- **Discord:** `@mirathatnardo`
- **Telegram:** `@mirathatnardo`
- **Email:** *Not configured yet; a dedicated project address may be added here later.*

If a more formal contact channel becomes necessary, I'll work to provide one. In the meantime, the project's [GitHub repository](https://github.com/ZettaBite4031/Aurore) is public.

Thanks for making a game that's inspired so much curiosity and creativity. I hope Aurore comes across as exactly what it is: an enthusiastic experiment by someone who enjoys Minecraft enough to wonder how its machinery works.

The Aurore developer
