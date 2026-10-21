# Source import policy

[← KairoTowns](../README.md)

This repository is independent. Its only Git remote is `origin`, pointing to this project's repository. The first-party Kairo frontend is pinned at `shared/` as a submodule. Do not add an `upstream` remote, fork relationship, other submodule, subtree sync, or automated upstream merge. Third-party source is copied in as pinned snapshots under `third_party/`.

## Tsugaru snapshot

`third_party/tsugaru/` holds the emulator source of [Tsugaru](https://github.com/captainys/TOWNSEMU) by Soji Yamakawa (CaptainYS).

| | |
| --- | --- |
| Release | `v20260522`, tag commit `7f3bc46234a67673f532f5016fc42b83c68e3c2b` (2026-05-22) |
| Archive | <https://codeload.github.com/captainys/TOWNSEMU/tar.gz/refs/tags/v20260522> |
| Archive SHA-256 | `59e535dc04ade2d85c1aa051dbe89de57ea4b76ace531911477f5e28799f5342` |
| Imported | 2026-10-21 |

Copied from the archive: `src/`, `LICENSE`, `readme.md`, `command.md`, `hdimage.md`, `references.txt`, and `FMTOWNS_Technical_Databook_Errata.md`.

Not copied: the desktop GUI (`gui/`, `winapp/`), guest-side programs and utilities (`dosapp/`, `townsapp/`, `util/`), test programs and disk images (`testasm/`, `testc/`, `testdata/`), `experiments/`, `symtables/`, `scripts/`, and `reminder.txt`. They are not part of the emulator library. `src/towns/townsdef/.gitignore` was removed so every copied file is tracked.

There are no local changes to the Tsugaru sources.
