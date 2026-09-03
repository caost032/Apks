#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${ODPAR_TEST_BUILD_DIR:-/tmp/odpar-whiteline-test-${UID:-0}-$$}"
CC="${CC:-clang}"
rm -rf "$BUILD"
mkdir -p "$BUILD"
cleanup(){ rm -rf "$BUILD" 2>/dev/null || true; }
trap cleanup EXIT
COMMON=(-std=c11 -O2 -Wall -Wextra -Werror -pedantic -ffp-contract=off -fPIC -fvisibility=hidden -I"$ROOT/engine/include")

# Compile the shared engine translation units once. The old gate compiled the
# ~large core repeatedly for core/render/module tests and wasted most of its time.
"$CC" "${COMMON[@]}" -c "$ROOT/engine/src/odwd_core.c" -o "$BUILD/odwd_core.o"
"$CC" "${COMMON[@]}" -c "$ROOT/engine/src/odwd_simple.c" -o "$BUILD/odwd_simple.o"
"$CC" "${COMMON[@]}" -c "$ROOT/engine/src/odwd_render.c" -o "$BUILD/odwd_render.o"

"$CC" "${COMMON[@]}" "$ROOT/engine/tests/test_core.c" "$BUILD/odwd_core.o" -lm -o "$BUILD/test_core"
"$BUILD/test_core"

# White-box generator/AUTO invariants intentionally include odwd_core.c so they
# can test static helpers without making test-only symbols public.
"$CC" "${COMMON[@]}" "$ROOT/engine/tests/test_survival_internal.c" -lm -o "$BUILD/test_survival"
"$BUILD/test_survival"

"$CC" "${COMMON[@]}" "$ROOT/engine/tests/test_render.c" \
  "$BUILD/odwd_core.o" "$BUILD/odwd_simple.o" "$BUILD/odwd_render.o" -lm -o "$BUILD/test_render"
"$BUILD/test_render"

"$CC" -shared -fvisibility=hidden \
  "$BUILD/odwd_core.o" "$BUILD/odwd_simple.o" "$BUILD/odwd_render.o" \
  -std=c11 -O2 -Wall -Wextra -Werror -pedantic -ffp-contract=off -fPIC \
  -I"$ROOT/engine/include" -I"$ROOT/module/include" \
  "$ROOT/module/src/whiteline_module.c" -lm -o "$BUILD/libodpar_whiteline.so"
exports="$(nm -D --defined-only "$BUILD/libodpar_whiteline.so" | awk '{print $3}' | sort)"
[[ "$exports" == "odpar_module_get_api" ]] || { echo "Unexpected module exports:" >&2; echo "$exports" >&2; exit 1; }
echo "WhiteLine module ABI surface: PASS (1 symbol)"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic -I"$ROOT/module/include" \
  "$ROOT/module/tests/test_module_host.c" -ldl -o "$BUILD/test_module_host"
"$BUILD/test_module_host" "$BUILD/libodpar_whiteline.so"

bash -n "$ROOT/tools/build_android_apk_no_gradle.sh"
echo "Android no-Gradle build script syntax: PASS"
echo "ODPAR WhiteLine Native gate: PASS"
