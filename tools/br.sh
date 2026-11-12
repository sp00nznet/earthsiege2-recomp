#!/bin/sh
# dev loop: build, then one headless run. tools/br.sh NAME [host args...]
#   tools/br.sh t1 --play --watchdog 60      (BR_EXE=es2sim for the sim alone)
cd "$(dirname "$0")/.." || exit 1
cmd.exe //c "$(cygpath -w "$PWD")\build.cmd > build.log 2>&1" || { grep -E " error |error LNK" build.log | head; exit 1; }
n=$1; shift
mkdir -p work
PCRECOMP_MUTE=1 timeout 400 build/${BR_EXE:-es2shell}.exe --headless "$@" > work/$n.log 2>&1; echo "rc=$?"
