#!/usr/bin/env sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ANDROID="$ROOT/app/android"
: "${FLUTTER_ROOT:?Set FLUTTER_ROOT to the Flutter SDK directory}"
printf 'flutter.sdk=%s\n' "$FLUTTER_ROOT" > "$ANDROID/local.properties"
if command -v gradle >/dev/null 2>&1; then
  (cd "$ANDROID" && gradle wrapper --gradle-version 9.3.1 --distribution-type bin)
else
  echo "Gradle 9.3.1 is required once to materialize the wrapper." >&2
  exit 2
fi
