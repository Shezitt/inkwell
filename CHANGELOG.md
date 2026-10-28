# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Reorder pages and folders, or move them into another folder, by dragging
  them in the sidebar.
- Move Up (Ctrl+Shift+Up) and Move Down (Ctrl+Shift+Down) for the selected
  page or folder, also in the sidebar's context menu.
- Quick switcher: File > Go to Page… (Ctrl+P) finds pages by name, with fuzzy
  matching, and opens the chosen one.

## [0.1.1] - 2026-10-28

### Fixed

- The sidebar showed no folders or pages after restarting, although they were
  saved.

## [0.1.0] - 2026-10-27

The first release: the foundation of Inkwell.

### Added

- Sidebar with nested folders and pages: create, rename inline and delete.
- Tabbed plain-text editor with closable, movable tabs.
- Automatic saving on a background thread, after a short pause and at least
  every 2 seconds while typing, and whenever an editor loses focus or closes.
- Durable storage in SQLite (WAL, `synchronous=FULL`) with versioned schema
  migrations.
- Session restore: open tabs, cursor and scroll positions, expanded folders,
  sidebar and window layout.
- Graceful shutdown that saves everything on SIGTERM, SIGINT, SIGHUP and
  desktop logout.
- Single-instance protection per notebook, robust against crashed instances.
- Status bar showing save status and cursor position.
- `--database` option to open a notebook from any location.
- Desktop entry and icon for Linux.

[Unreleased]: https://github.com/Shezitt/inkwell/compare/v0.1.1...HEAD
[0.1.1]: https://github.com/Shezitt/inkwell/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/Shezitt/inkwell/releases/tag/v0.1.0
