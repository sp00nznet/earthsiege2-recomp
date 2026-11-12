# Contributing

Issues and pull requests are welcome.

## The one rule

**Nothing from the game goes in the repository.** No game files, no lifted C
(`src/recomp/gen/`), no catalogs or seeds (`work/`), no disassembly
listings, nothing generated from `VSHELL.EXE` or `DBSIM.EXE`. The tool ships;
its output is made on each person's machine from their own copy.
`.gitignore` covers the usual paths; check `git status` before you commit
anyway. Screenshots of the game running are fine.

## Where your code comes from

Code must be your own or under an MIT-compatible licence. Don't port from
GPL projects (Wine, DOSBox, ScummVM and the like), even rewritten; if a fix
came from somewhere, say where in the pull request.

## Before a pull request

There is no conformance harness yet (see [ROADMAP.md](ROADMAP.md)). Until
there is:

1. Rebuild from a clean lift: `py -3 tools\catalog.py --force`,
   `py -3 run_lift.py` (expected: `lift errors 0` for shell and sim), then
   `build.cmd`.
2. Run the game headless into mission 1 and look at the shots:
   ```
   build\es2shell.exe --play --headless --skip-movies --watchdog 120 --shot 60 work\t.bmp
   ```
   [docs/building.md](docs/building.md) has the scripted-input options.
   Headless runs are muted.

## Where a fix belongs

- A bug in the lifter, the catalog, `drive32` or native32 belongs in
  [pcrecomp](https://github.com/sp00nznet/pcrecomp), on a branch and a pull
  request, not here.
- Something only Earth Siege 2 does (a shim, a seed scan for Borland's code
  shapes, the `--play` loop) belongs in `src/runtime/host.c` or
  `tools/catalog.py`, with a comment that says what it fixes and why.
- A wall you got past goes in `docs/bringup.md`: what it looked like, what it
  was, what moved it.
