# Building

Windows only (see [ROADMAP.md](../ROADMAP.md) for Linux and macOS).
`Setup.cmd` runs these same steps; this is the by-hand route.

## Prerequisites

- Windows 10 or 11
- Python 3.10 or newer, with `pefile` and `capstone`
  (`py -3 -m pip install pefile capstone`)
- Visual Studio 2022 or its Build Tools, with *Desktop development with
  C++* (CMake and Ninja come with it)
- git, and **pcrecomp** (`main`), cloned beside this repository;
  `tools/setup.ps1` checks it has the headless desktop, the module loader,
  the guest registry, synthetic DirectInput and `drive32`.

## Steps

```
git clone https://github.com/sp00nznet/pcrecomp
git clone https://github.com/sp00nznet/earthsiege2-recomp
cd earthsiege2-recomp
mklink /J game D:\es2                  (a writable copy of your CD)
echo D:\es2> game\DATA\drive.cfg & echo D:\es2>> game\DATA\drive.cfg
<nul set /p=E> game\DATA\language.cfg
py -3 tools\catalog.py                 (about 3 minutes)
py -3 run_lift.py
build.cmd
build\es2shell.exe --play --skip-movies
```

`run_lift.py` prints one line per program, with `lift errors 0`.
`build.cmd` builds `build\es2shell.exe` and `build\es2sim.exe`. A pcrecomp
somewhere else: `set PCRECOMP=...` before the Python steps, and
`set CMAKE_ARGS=-DPCRECOMP=...` before `build.cmd`.

`drive.cfg` and `language.cfg` are what the original installer's
`BATCH.EXE` wrote: the CD path and install path, and the language.

## Running

```
es2shell --play [host options]                the whole game (ES.EXE's loop)
es2shell|es2sim --run [host options] [game switches]   one program
  --headless            on a private desktop nothing reaches your screens (muted)
  --skip-movies         end every movie at once
  --shot S FILE.bmp     a screenshot S seconds in (repeatable)
  --record FILE.mp4     every presented frame (ffmpeg on PATH)
  --key VK@S[+HOLD]     press a key S seconds in, held HOLD seconds
  --click X,Y@S         click at a window point S seconds in
  --watchdog S          stop after S seconds and say where it was
```

The sim alone, on the mission the shell last set up:
`build\es2sim.exe dummy -eggplant -X2 -R0 --run`.

`tools/br.sh NAME [args]` (Git Bash) is the dev loop: build, then one
headless run with its log in `work\NAME.log`.
