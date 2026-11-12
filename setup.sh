#!/bin/sh
# Earth Siege 2 builds on Windows only, for now. The host is pcrecomp's
# runtime/native32, which is MSVC x86 inline assembly, so there is no native
# Linux or macOS build, and a clang-cl cross build under Wine has not been
# tried. Use Setup.cmd on Windows (see README.md).
echo "setup.sh: Earth Siege 2 builds only on Windows so far (Setup.cmd). No Linux or macOS path yet; see ROADMAP.md." >&2
exit 1
