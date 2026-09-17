# Legal and Provenance Policy

> [!IMPORTANT]
> Aurore is not an official Minecraft product and is not approved by or associated with Mojang Studios or Microsoft.

This is a conservative project policy, not legal advice. It exists to prevent accidental inclusion or distribution of material that Aurore does not own or have permission to redistribute.

## Independent implementation

Aurore is intended to be an original implementation based on public specifications, independently written notes, black-box interoperability testing, lawfully observed network behavior, original fixtures, and appropriately licensed dependencies.

Do not incorporate copied or mechanically translated game source, decompiled implementation material, proprietary comments, official assets, or substantial protected content. Record compatibility behavior in implementation-neutral terms: inputs, outputs, ordering, limits, and failure behavior.

## Repository boundary

The tracked repository may contain:

- original Aurore source, documentation, schemas, and synthetic fixtures;
- third-party material whose license permits the intended use and whose notices are preserved;
- small factual compatibility metadata with recorded provenance;
- reproducible generators that operate on user-selected local inputs.

It must not contain:

- official client or server software;
- official textures, models, audio, fonts, icons, or other assets;
- copied or decompiled implementation code;
- complete extracted registry, tag, report, or data-pack content without established redistribution rights;
- user-derived local caches by default;
- credentials, tokens, private keys, account/session data, or identifying packet captures;
- material whose source is merely unknown or "found online."

## Data classes

| Class | Repository | Release |
|---|---:|---:|
| Original Aurore material | Allowed | Allowed under its stated license |
| Verified openly licensed material | With source, revision, license, and notices | When all obligations are met |
| User-supplied official input | Prohibited | Prohibited |
| Data derived locally from user input | Prohibited by default | Prohibited by default |
| Restricted reference material | Prohibited | Prohibited |
| Unknown or unreviewed material | Prohibited | Prohibited |

Generating a file does not automatically create redistribution permission for the input or output.

## Local data generation

A future generator should require an explicit local input, hash that input, identify the target version and generator revision, produce deterministic output where practical, record output hashes and schema versions, and fail closed when provenance cannot be written.

Generated caches belong outside the tracked tree, for example under `.local/`, and should be invalidated when the source hash, generator, schema, or target version changes. They must never include secrets or personal absolute paths in their manifests.

Automatic downloading of official software is outside the currently approved model and requires a fresh terms and distribution review.

## Trademark and presentation

Aurore must remain the dominant project name. Minecraft references are descriptive compatibility references and must not imply sponsorship or official status. Do not use official logos, assets, or trade dress as Aurore branding.

Public repositories, releases, websites, and user interfaces should display the non-affiliation statement and identify the actual project publisher. Add a durable public contact method before distributing binaries or offering a hosted or paid service.

## Public-release gate

Before the first public binary or generally available server package:

1. verify the copyright holder and license text;
2. review the then-current official EULA, usage guidelines, community standards, and relevant service terms;
3. verify every dependency's exact revision, license, and notice obligations;
4. inspect the release allowlist for official software, assets, local caches, credentials, and unreviewed generated data;
5. document supported versions, authentication mode, omissions, and user responsibilities;
6. resolve the intended vanilla-data acquisition and cache model;
7. seek qualified legal review if distribution, monetization, automated acquisition, authentication, or data reuse creates material uncertainty.

## Third-party dependencies

Current dependency sources and pins are recorded in [Third-party notices](../THIRD_PARTY_NOTICES.md). A permissive license on a tool does not necessarily license data produced by that tool.

Revisit this policy when the project becomes public, prepares a binary release, adds online authentication, changes its data model, adds a hosted or paid offering, or introduces a new external dataset.
