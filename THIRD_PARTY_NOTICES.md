# Third-Party Notices

Aurore uses source dependencies as exact Git submodule revisions. These notes record their purpose and do not replace the license text shipped by each dependency.

## Sonnet

- Repository: <https://github.com/ZettaBite4031/Sonnet>
- Path: `vendor/Sonnet`
- Purpose: JSON parsing and serialization
- Pin in the cleaned source baseline: `0d07d323cf27a71058d30552928b138847e5905c`

Confirm the license and required notices at the pinned revision before distributing binaries that include Sonnet.

## GoogleTest

- Repository: <https://github.com/google/googletest>
- Path: `vendor/googletest`
- Purpose: automated C++ testing
- Pin in the cleaned source baseline: `a0f06a70e3da7afa88da9527c43951bca1f7cef2`
- License: BSD-3-Clause at the pinned revision

## Dependency policy

Dependency updates are intentional, reviewed changes. Do not configure release builds to follow upstream branch heads. A new or updated dependency requires a recorded source, exact revision, purpose, license review, and successful Debug and Release test runs.
