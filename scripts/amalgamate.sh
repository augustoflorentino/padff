#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$root/dist/padff.h}
mkdir -p "$(dirname "$out")"

{
    cat "$root/include/padff/padff.h"
    echo
    echo "#if defined(PFF_IMPLEMENTATION) && !defined(PFF_IMPLEMENTATION_DONE)"
    echo "#define PFF_IMPLEMENTATION_DONE"
    for src in "$root"/src/*.h "$root"/src/*.c; do
        [ -f "$src" ] && grep -v '^#include "' "$src"
    done
    echo "#endif"
} > "$out"
echo "$out"
