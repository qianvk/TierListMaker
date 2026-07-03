# Project Format

TierListMaker projects use UTF-8 JSON with the extension `.tlmproject`.

The root object contains:

- `schemaVersion`: integer, currently `1`.
- `app`: writer metadata.
- `project`: id, name, path-derived metadata, and timestamps.
- `tiers`: ordered tier rows with stable ids.
- `images`: image entries with source/asset paths and assignment metadata.
- `settings`: project-level settings.

Unknown future fields are ignored when safe. Required malformed fields cause a clear validation error instead of a crash.

Asset paths are stored relative to the project directory where possible.

