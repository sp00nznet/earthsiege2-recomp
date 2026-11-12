#!/usr/bin/env python3
"""Function catalogs for VSHELL.EXE (the shell) and DBSIM.EXE (the sim), with
pcrecomp's own disasm32. Borland C++ (no MSVC RTTI to seed from).
Skipped when a catalog exists (--force redoes it).

    py -3 tools/catalog.py
Output: work/<mod>.seeds.json, work/<mod>.functions.json, log in work/catalog.log
"""
import json
import os
import struct
import subprocess
import sys

import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(os.environ.get('PCRECOMP', os.path.join(ROOT, '..', 'pcrecomp')), 'tools')
WORK = os.path.join(ROOT, 'work')
MODULES = {'shell': 'VSHELL.EXE', 'sim': 'DBSIM.EXE'}
PROLOGUE = bytes([0x55, 0x8B, 0xEC])   # push ebp; mov ebp, esp


def data_pointer_seeds(exe):
    """Code addresses stored anywhere in the image (data, or an immediate in
    code: `push 0x004311B8`) that open with Borland's
    `push ebp; mov ebp, esp`: virtual methods and callbacks, at any byte
    offset (Borland packs its structs: VSHELL keeps a method pointer at +1). Borland keeps its
    vtables in DATA and its strings in CODE, so a method that only a vtable
    names, sitting right after a string, is found by neither of disasm32's
    scans (VSHELL's method at 0x00453741 was the first)."""
    pe = pefile.PE(exe, fast_load=True)
    img = pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase
    is_code = lambda s: s.Characteristics & 0x20000000  # noqa: E731
    code = [(base + s.VirtualAddress, base + s.VirtualAddress + s.Misc_VirtualSize) for s in pe.sections if is_code(s)]
    seeds = set()
    for s in pe.sections:
        for off in range(s.VirtualAddress, min(s.VirtualAddress + s.Misc_VirtualSize, len(img)) - 3):
            v = struct.unpack_from('<I', img, off)[0]
            if any(a <= v < b for a, b in code) and img[v - base:v - base + 3] == PROLOGUE:
                seeds.add(v)
    return sorted(seeds)


def unrolled_entries(exe):
    """Every way into the hand-written span fillers' unrolled loops:
    `mov edx, END; sub edx, eax; ...; jmp edx` enters a run of identical
    8-byte groups (`stosw; add edi, ecx; add ebp, ebx; adc eax, esi`) that
    ends at END, count groups before its end (a Duff's device; VSHELL's
    polygon filler at 0x004566C5 runs 400 of them). Each group start is an
    entry; drive32 makes them stubs into the one body that holds them all."""
    pe = pefile.PE(exe, fast_load=True)
    img = pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase
    seeds = set()
    for s in pe.sections:
        if not s.Characteristics & 0x20000000:
            continue
        lo, hi = s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize
        for off in range(lo, hi - 5):
            r = img[off] - 0xB8
            if not 0 <= r < 8:
                continue
            end = struct.unpack_from('<I', img, off + 1)[0] - base
            tail = img[off + 5:off + 29]
            jmp = bytes([0xFF, 0xE0 + r])
            if not lo + 16 <= end < hi or jmp not in tail[:tail.find(jmp) + 2]:
                continue
            for period in range(2, 17):
                group = img[end - period:end]
                k = 0
                while end - period * (k + 1) >= lo and img[end - period * (k + 1):end - period * k] == group:
                    k += 1
                if k >= 8:
                    seeds.update(base + end - period * i for i in range(k + 1))
                    break
    return seeds


def main():
    os.makedirs(WORK, exist_ok=True)
    with open(os.path.join(WORK, 'catalog.log'), 'a') as log:
        for mod, exe in MODULES.items():
            cat = os.path.join(WORK, mod + '.functions.json')
            if os.path.exists(cat) and '--force' not in sys.argv:
                print('%s: catalog already there (--force redoes it)' % mod)
                continue
            img = os.path.join(ROOT, 'game', exe)
            seeds = os.path.join(WORK, mod + '.seeds.json')
            json.dump(sorted(set(data_pointer_seeds(img)) | unrolled_entries(img)), open(seeds, "w"))
            cmd = [sys.executable, os.path.join(TOOLS, 'disasm', 'disasm32.py'), img, '-o', cat, '--seed-functions', seeds]
            if subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT).returncode:
                print('%s: disasm32 failed, see work/catalog.log' % exe)
                return 1
            print('%s: done' % mod)
    return 0


if __name__ == '__main__':
    sys.exit(main())
