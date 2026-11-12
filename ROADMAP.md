# Roadmap

## Next
- **`--play` end to end with input**: the sim faulted (read of 0 in
  `sub_00465BC0`, its palette setup: `GetDC(0)`, `GetSystemPaletteEntries`)
  when the shell's scripted clicks were replayed into it; run alone it is
  fine. Per-child input scripts, then find the fault.
- **Fire weapons, finish mission 1**, and the hand-back to the shell
  (`data\results.dat`; an assert in `src\game.cpp` line 362 fires when the
  shell starts at state 4 without one).
- **Movies**: decode Indeo 3 (ffmpeg as a sidecar) instead of skipping them.
- **CD audio music** from the disc's audio tracks (a cue/bin reader behind
  MCI cdaudio).
- **Upstream** the two catalog seed scans (`tools/catalog.py`: Borland
  method pointers at any offset, unrolled span loops) into pcrecomp's
  disasm32.
- A conformance harness (REPO_RULES section 9), a netlab recipe, Setup.cmd
  from a fresh clone.

## Later
- 256-colour full screen through DirectDraw (the 1996 path), joystick,
  multiplayer (IPX/modem, never on modern Windows).
