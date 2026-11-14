# Roadmap

## Done

- Both programs lifted whole with 0 errors, each in its own host, with no
  commercial tools.
- The shell from the intro to Rock & Roll, and mission 1 in the sim: the
  terrain renderer, cockpit, HUD, scanner and map, the HERC under keyboard
  control.
- Headless runs, screenshots and recordings.
- Setup.cmd from a fresh clone to a shortcut.

## Next

In order:

1. **`--play` end to end with input**: the sim faulted (read of 0 in
   `sub_00465BC0`, its palette setup: `GetDC(0)`, `GetSystemPaletteEntries`)
   when the shell's scripted clicks were replayed into it; run alone it is
   fine. Per-child input scripts, then find the fault.
2. **A conformance harness** (boot milestones in the shell and the sim,
   checked headless) and a netlab recipe.
3. **Fire weapons, finish mission 1**, and the hand-back to the shell
   (`data\results.dat`; an assert in `src\game.cpp` line 362 fires when the
   shell starts at state 4 without one).
4. **Movies**: decode Indeo 3 (ffmpeg as a sidecar) instead of skipping them.
5. **CD audio music** from the disc's audio tracks (a cue/bin reader behind
   MCI cdaudio).
6. **Upstream** the two catalog seed scans (Borland method pointers at any
   offset, unrolled span loops) into pcrecomp's `disasm32`.

## Deferred

- 256-color full screen through DirectDraw (the 1996 path), and joysticks.
- Linux and macOS: native32 is MSVC x86 inline assembly; a clang-cl cross
  build under Wine is the likely first path.

## Out of scope

- Distributing the game, its data, or the lifted C.
- Multiplayer over IPX or modem.
