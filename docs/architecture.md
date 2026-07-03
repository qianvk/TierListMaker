# Architecture

TierListMaker is split into small modules:

- `app` owns startup and service construction.
- `window` owns the frameless shell, app title bar, routing, and shortcuts.
- `navigation` implements the sidebar model/view/delegate.
- `pages` contains Edit, Projects, and Preferences pages.
- `tier` contains domain data and tier board widgets.
- `persistence` handles JSON serialization, atomic saves, project repository operations, and recent-project metadata.
- `assets` handles import paths, local asset migration, image loading, and thumbnails.
- `export` renders projects to PNG, JPEG, or PDF.
- `settings`, `theme`, and `i18n` centralize app-wide state.
- `platform` isolates Finder/Explorer/file-manager behavior.

Domain data uses stable UUIDs rather than indexes. The UI mutates a `TierProject`, emits change signals, and persistence serializes the project into a readable schema-versioned JSON file.

