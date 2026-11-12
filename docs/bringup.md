# Bring-up notes

What Earth Siege 2 needed, in the order it turned up. Everything here lives in
this repository (`src/runtime/host.c`, `tools/catalog.py`); pcrecomp's
`pcrecomp-ingame` branch was enough as it is.

## The copy of the game

The archive.org item is a BIN/CUE: one MODE1/2352 data track and six CD audio
tracks (the music). The data track's 2048-byte user data, read sector by
sector, is an ISO 9660 image that 7-Zip unpacks. No copy protection: no
SafeDisc, no driver, and the only CD check is the shell finding `avi\pt1.avi`
under the path in `DATA\drive.cfg`.

The installer (`SETUP.EXE`, Sierra's script in `SIERRA.INF`) only copies files
and runs `BATCH.EXE`, which writes `DATA\drive.cfg` (two lines: the CD path
and the install path, `%s\n%s\n`) and `DATA\language.cfg` (`E`). The game runs
from a writable copy of the whole disc with those two files added; Setup.cmd
writes them.

## Two programs and a launcher

`ES.EXE` (8 KB) checks memory and then loops on a state, starting at 1. It runs
a child with `spawnv` and takes its exit code as the next state, until 0:

| State | Child |
|---|---|
| 1, 3, 4, 6 | `vshell dummy -eggplant -X<state>` |
| 2, 5 | `dbsim dummy -eggplant -X<state> -R<n>` (n counts sim runs; 5 adds `-D`) |

`-eggplant` is the "started by ES.EXE" password. The shell writes
`DATA\mission.var` and the sim reads it. Both executables are based at
0x00400000, so each is lifted into its own host; `es2shell --play` is
ES.EXE's loop over `es2shell.exe` and `es2sim.exe`, with the children in a
kill-on-close job.

The game reads its switches from the command line itself (cw3220 asks
Windows), so the host ignores single-dash arguments and the game ignores the
host's `--` ones. With 30-odd arguments the shell fails with "Init Error:
Unable to initialize Instance", so `--play` passes its options to the
children in `ES2_OPTS` instead.

Host fault exit codes are 0x102-0x104, so `--play` never mistakes a crash
for a state.

## Lifting

Borland C++ 4.5/5.0, the runtime in `cw3220.dll` (loads natively, and calls
back into the lifted code through native32's callback trampoline).

- **Methods only a vtable names.** Borland keeps strings in CODE and vtables
  in DATA, at any byte offset (it packs structs: a method pointer at `+1`).
  The first hole was `ICALL: unresolved VA 0x00453741`, a method right after
  a string. `tools/catalog.py` seeds every dword anywhere in the image that
  points at `push ebp; mov ebp, esp` in code (765 shell, 1,036 sim seeds).
- **Unrolled span loops.** The hand-written polygon fillers enter a run of
  identical 8-byte groups (`stosw; add edi, ecx; add ebp, ebx; adc eax, esi`)
  by `mov edx, END; sub edx, count*8; jmp edx`, a Duff's device of 400 groups.
  The first hole was `ITAIL: unresolved VA 0x00457328`. `catalog.py` finds
  each `mov reg, imm; ...; jmp reg` whose target ends such a run and seeds
  every group start; pcrecomp's `drive32` makes them stubs into the one body
  that holds the run. Two runs per module.
- Result: shell 4,142 entries (2,496 bodies), sim 6,204 (3,505 bodies),
  0 errors.

## Drawing

Both programs draw into one 8-bit top-down DIB section (`biHeight` -480) and
`StretchBlt`/`BitBlt` it to the window on `WM_PAINT`; DirectDraw is imported
but not used on a 32-bit desktop (the shell warns "You are not in 256 color
mode" and carries on). Under `--headless` a GDI copy of the window is black,
so the host wraps both blits (`native32_redirect`) and keeps the last source
DIB, its colour table and the source rectangle; `--shot` and `--record` read
those. `GetObject` reports the top-down DIB's height positive, so the host
treats it as top-down.

## Movies

The intro (`avi\intr_pt1.avi`, `intr_pt2.avi`, 87 s) and the briefings play
through MCI (`open avivideo`, `play mov window notify`) and the shell waits
for `MM_MCINOTIFY`. They are Indeo 3; current Windows has no decoder.
`--skip-movies` answers each `play ... notify` with an immediate
`MM_MCINOTIFY` (successful).

The shell parses MCI's `status mov window handle` answer with `scanf`, not
`sscanf`, so it reads stdin; the host gives it `NUL` (before cw3220 loads,
which takes its handles at attach) instead of a console that blocks.

## Input

Both programs use window messages, `GetCursorPos`/`ScreenToClient` and
`GetKeyState`. On the private desktop `GetCursorPos` fails, so under
`--headless` the host answers the last `--click` point, and `GetKeyState`
answers the keys `--key` is holding. In the sim the arrow keys turn the HERC
and throttle it up (`--key 0x26@25+8`: 99 km/h).

## Sound

The SOS library (`sos9503.dll`, `soslibs3.dll`) loads natively. Music is CD
audio (MCI cdaudio), absent with no disc in a drive.
