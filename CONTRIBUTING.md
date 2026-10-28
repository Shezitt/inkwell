# Contributing to Inkwell

Thanks for helping out! Bug reports, ideas and pull requests are all welcome.
This guide explains how the project is organised.

## Ground rules

- Be kind. This project follows the [Code of Conduct](CODE_OF_CONDUCT.md).
- **Never lose user data.** Any change that touches saving, loading or the
  database schema must keep the [durability guarantees](docs/ARCHITECTURE.md#durability)
  intact and come with tests.
- **Stay fast.** Inkwell must feel instant. Never do disk I/O on the UI thread
  in a hot path (for example on every keystroke), and avoid loading page bodies
  you do not need.

## Getting started

Install a C++20 compiler, CMake ≥ 3.21 and Qt ≥ 6.4 (Core, Gui, Widgets, Sql
and Test, plus the SQLite driver). On Debian/Ubuntu:

```sh
sudo apt install build-essential cmake qt6-base-dev libqt6sql6-sqlite libgl-dev
```

Then build and run the tests:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
./build/dev/src/inkwell
```

The `asan` preset builds with AddressSanitizer and UBSan. Please run it
before sending changes to the storage layer.

## Workflow

`main` is always releasable and protected. All work happens on short-lived
branches named by type:

| Prefix      | Use for                                   |
| ----------- | ----------------------------------------- |
| `feat/`     | new user-facing functionality             |
| `fix/`      | bug fixes                                 |
| `perf/`     | performance work                          |
| `refactor/` | internal changes without behaviour change |
| `docs/`     | documentation only                        |
| `ci/`, `build/`, `chore/` | tooling and housekeeping    |

Open a pull request against `main`. CI must be green before it is merged.

## Commits

Commits follow [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<optional scope>): <imperative summary, max ~72 chars>

<optional body explaining *why*, wrapped at 72 columns>
```

Common types are `feat`, `fix`, `perf`, `refactor`, `test`, `docs`, `build`,
`ci`, `style` and `chore`. Scopes match the source tree: `core`, `ui`, `app`.

Keep commits **atomic**. Each commit should make one logical change, build on
its own and pass the tests, so that `git bisect` and `git revert` stay useful.
Put tests in the same commit as the code they cover.

## Code style

- Format C++ with the repository's `.clang-format` (clang-format 19):
  `clang-format -i path/to/file.cpp`. CI rejects unformatted code.
- Follow Qt conventions: `CamelCase` types, `camelCase` functions, `m_` member
  prefix, `k` prefix for constants.
- Strings are explicit: the build defines `QT_NO_CAST_FROM_ASCII`, so use
  `u"..."_s` (with `using namespace Qt::StringLiterals;`) or `tr("...")` for
  user-visible text.
- `src/core` must not depend on Qt Widgets. Keep logic there and test it.
- Start every source file with `// SPDX-License-Identifier: MIT`.

## Reporting bugs

Use the issue templates. For anything involving lost or corrupted notes,
include your OS, the Inkwell version, and what happened right before the
problem (crash, power loss, forced quit, …).
