#!/usr/bin/env bash
# Build Kairos and install it into the local Geode mods folder.  ./deploy.sh [--no-install]
set -u
cd "$(dirname "$0")"
# The bindings commit gdsolver's builds use (newer ones made GJGameState non-copyable).
export GEODE_BINDINGS_REPO_PATH="$(cd .. && pwd -W)/Kairos-bindings"
MODS="/c/Users/Kusmic/Documents/GeodeModing/geode/mods"

mkdir -p build
cmake -S . -B build -A x64 > build/configure.log 2>&1 || { tail -20 build/configure.log; echo "x configure failed"; exit 1; }
cmake --build build --config RelWithDebInfo > build/build.log 2>&1
if grep -E "error C|error LNK|fatal error" build/build.log | sort -u | head -20 | grep .; then echo "x build failed"; exit 1; fi
[ -f build/kusmic.kairos.geode ] || { echo "x no .geode produced"; exit 1; }
[ "${1:-}" = "--no-install" ] && { echo "ok built"; exit 0; }
[ -f "$MODS/gdsolver.solver.geode" ] && echo "! gdsolver.solver is installed too: disable it in Geode (both hook the same functions)"
cp build/kusmic.kairos.geode "$MODS/" && echo "ok installed $(date '+%H:%M:%S')"
tasklist 2>/dev/null | grep -qi "GeometryDash" && echo "! Geometry Dash is running: restart it to load this build"
exit 0
