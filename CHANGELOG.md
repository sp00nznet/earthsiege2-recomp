# Changelog

All notable changes to this project. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow [SemVer](https://semver.org/).

## [Unreleased]

### Added
- `VSHELL.EXE` and `DBSIM.EXE` lifted whole (pcrecomp `disasm32` and
  `drive32`, no IDA) into two native32 hosts, `es2shell.exe` and
  `es2sim.exe`; `es2shell --play` does what `ES.EXE` did.
- Catalog seeds for Borland's vtable methods and the unrolled span loops
  (`tools/catalog.py`).
- GDI frame capture for `--shot` and `--record`, `--skip-movies`, and the
  cursor and key state under `--headless`.
- In game: mission 1, the HERC walking and turning under keyboard control.
- `Setup.cmd` and `tools/setup.ps1`, tested from a fresh clone; the
  installer workflow (by hand only) and `launcher/launcher.json`.
- `NOTICE.md`, `CONTRIBUTING.md`, `CONTRIBUTORS.md`, `docs/building.md`;
  `setup.sh`, which says there is no Linux or macOS build yet.
