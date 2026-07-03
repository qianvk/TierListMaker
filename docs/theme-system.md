# Theme System

`ThemeManager` exposes semantic tokens rather than raw palette choices:

- `windowBackground`
- `sidebarBackground`
- `contentBackground`
- `separator`
- `primaryText`
- `secondaryText`
- `accent`
- `selection`
- `tierRowBackground`

The app supports System, Light, and Dark appearance. Widgets listen for `themeChanged` and repaint or update styles immediately.

