# Third-Party Notices

Aurore uses pinned source dependencies as Git submodules. This document records the revisions present in the **First Light documentation baseline**. The real gitlink SHAs in a tagged commit are authoritative; when pins change, update this file too.

## Sonnet

- **Purpose:** JSON parsing and serialization, including server configuration.
- **Repository:** <https://github.com/ZettaBite4031/Sonnet>
- **Submodule path:** `vendor/Sonnet`
- **Reviewed pin:** `0d07d323cf27a71058d30552928b138847e5905c`
- **License status at this reviewed pin:** No explicit license file was found in the repository tree.

Sonnet's developer has stated the intention to apply an attribution-preserving permissive open-source license, preferably MIT. **Intent is not a substitute for a published license grant.** Before distributing First Light binaries that include Sonnet, add and commit an appropriate license in the Sonnet repository, update Aurore's submodule pin, and revise this notice to point to the actual license and final pin. Preserve the license text and any required notices in binary distributions.

Until that is done, do not describe this specific Sonnet revision as MIT-licensed or assume redistribution permission has already been documented.

## GoogleTest

- **Purpose:** Aurore automated testing; not needed to run the packaged server.
- **Repository:** <https://github.com/google/googletest>
- **Submodule path:** `vendor/googletest`
- **Reviewed pin:** `f840327b0f76408eafde2440af3b2f0a70662ea6`
- **License:** BSD-3-Clause (see `vendor/googletest/LICENSE` at the pinned revision).

The GoogleTest pin above is the version currently recorded by Aurore's Git tree and supersedes the older revision previously written in this notice. Include its license when distributing a source bundle containing that dependency, and follow any notice requirements of the exact revision.

## Platform/runtime dependencies

Windows packages may require a compatible Microsoft Visual C++ runtime. Linux packages may rely on system-provided C/C++ and libcurl shared libraries. Inspect the binaries actually built for release and document the real dependencies; do not assume the CI machine's installed libraries exist everywhere.

## Dependency updates

For each dependency change:

1. Record the exact submodule revision used by the release commit.
2. Verify its license and relevant notices at that revision.
3. Make sure source/binary packages include required license material.
4. Validate Debug and Release builds/tests on Windows and Linux.
5. Update this document before tagging.

The [MIT license](LICENSE.txt) covers Aurore-authored material, not every dependency or upstream Minecraft data the project may interact with. Read [Legal and Provenance](docs/LEGAL_AND_PROVENANCE.md) for the distribution boundary.
