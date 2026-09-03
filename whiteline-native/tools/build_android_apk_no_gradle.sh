#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${ODPAR_ANDROID_OUT:-$ROOT/dist/android}"
MIN_API="${ODPAR_ANDROID_MIN_API:-26}"

fail(){ printf 'ERROR: %s\n' "$*" >&2; exit 1; }
note(){ printf '%s\n' "$*"; }

find_sdk() {
  if [[ -n "${ANDROID_SDK_ROOT:-}" && -d "$ANDROID_SDK_ROOT" ]]; then
    printf '%s\n' "$ANDROID_SDK_ROOT"; return 0
  fi
  if [[ -n "${ANDROID_HOME:-}" && -d "$ANDROID_HOME" ]]; then
    printf '%s\n' "$ANDROID_HOME"; return 0
  fi
  local p
  for p in "$HOME/Android/Sdk" "$HOME/android-sdk" "/opt/android-sdk" "/usr/local/lib/android/sdk"; do
    [[ -d "$p" ]] && { printf '%s\n' "$p"; return 0; }
  done
  return 1
}

SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
if [[ -z "$SDK" || ! -d "$SDK" ]]; then SDK="$(find_sdk || true)"; fi
[[ -n "$SDK" && -d "$SDK" ]] || fail "Android SDK not found. Set ANDROID_SDK_ROOT once."

# Use an explicit NDK when supplied; otherwise choose the newest installed NDK
# that actually contains the LLVM Android toolchain. No hard-coded NDK revision.
NDK="${ANDROID_NDK_ROOT:-${ANDROID_NDK_HOME:-}}"
if [[ -z "$NDK" || ! -d "$NDK" ]]; then
  mapfile -t ndk_candidates < <(
    {
      [[ -d "$SDK/ndk" ]] && find "$SDK/ndk" -mindepth 1 -maxdepth 1 -type d -print
      [[ -d "$SDK/ndk-bundle" ]] && printf '%s\n' "$SDK/ndk-bundle"
    } | sort -V
  )
  for ((i=${#ndk_candidates[@]}-1; i>=0; --i)); do
    candidate="${ndk_candidates[$i]}"
    if compgen -G "$candidate/toolchains/llvm/prebuilt/*/bin/*-linux-android${MIN_API}-clang" >/dev/null ||
       compgen -G "$candidate/toolchains/llvm/prebuilt/*/bin/aarch64-linux-android${MIN_API}-clang" >/dev/null; then
      NDK="$candidate"; break
    fi
  done
fi
[[ -n "$NDK" && -d "$NDK" ]] || fail "Android NDK not found. Install one SDK NDK or set ANDROID_NDK_ROOT."

# Detect the NDK host prebuilt directory by contents instead of assuming an NDK
# revision or host tag.
TC=""
for candidate in "$NDK"/toolchains/llvm/prebuilt/*/bin; do
  [[ -x "$candidate/aarch64-linux-android${MIN_API}-clang" ]] || continue
  TC="$candidate"; break
done
[[ -n "$TC" ]] || fail "NDK found, but ARM64 clang for API $MIN_API is missing: $NDK"

CC="$TC/aarch64-linux-android${MIN_API}-clang"
STRIP="$TC/llvm-strip"

# Newest installed build-tools that contains only the three tools we use.
if [[ -n "${ODPAR_BUILD_TOOLS:-}" ]]; then
  BT="$SDK/build-tools/$ODPAR_BUILD_TOOLS"
else
  BT=""
  mapfile -t bt_candidates < <(find "$SDK/build-tools" -mindepth 1 -maxdepth 1 -type d -print 2>/dev/null | sort -V)
  for ((i=${#bt_candidates[@]}-1; i>=0; --i)); do
    candidate="${bt_candidates[$i]}"
    [[ -x "$candidate/aapt2" && -x "$candidate/zipalign" && -x "$candidate/apksigner" ]] || continue
    BT="$candidate"; break
  done
fi
[[ -n "$BT" && -d "$BT" ]] || fail "Android build-tools with aapt2/zipalign/apksigner not found."

AAPT2="$BT/aapt2"
ZIPALIGN="$BT/zipalign"
APKSIGNER="$BT/apksigner"

# Compile against the newest installed Android platform unless explicitly fixed.
if [[ -n "${ODPAR_ANDROID_PLATFORM:-}" ]]; then
  PLATFORM="${ODPAR_ANDROID_PLATFORM#android-}"
else
  PLATFORM=""
  mapfile -t platform_candidates < <(find "$SDK/platforms" -mindepth 1 -maxdepth 1 -type d -name 'android-*' -print 2>/dev/null | sort -V)
  for ((i=${#platform_candidates[@]}-1; i>=0; --i)); do
    candidate="${platform_candidates[$i]}"
    [[ -f "$candidate/android.jar" ]] || continue
    PLATFORM="${candidate##*/android-}"; break
  done
fi
[[ "$PLATFORM" =~ ^[0-9]+$ ]] || fail "No usable Android platform found under $SDK/platforms."
(( PLATFORM >= MIN_API )) || fail "Compile platform API $PLATFORM is lower than min API $MIN_API."
ANDROID_JAR="$SDK/platforms/android-$PLATFORM/android.jar"

for f in "$CC" "$STRIP" "$AAPT2" "$ZIPALIGN" "$APKSIGNER" "$ANDROID_JAR"; do
  [[ -e "$f" ]] || fail "Missing required tool/file: $f"
done
command -v zip >/dev/null || fail "zip command not found"

# Stable test signing. The repository may carry this DEBUG keystore so repeated
# sideloaded builds upgrade cleanly. Production can override every value below.
KEYSTORE="${ODPAR_KEYSTORE:-$ROOT/signing/odpar-debug.keystore}"
KEY_ALIAS="${ODPAR_KEY_ALIAS:-androiddebugkey}"
STORE_PASS="${ODPAR_KEYSTORE_PASSWORD:-android}"
KEY_PASS="${ODPAR_KEY_PASSWORD:-android}"
if [[ ! -f "$KEYSTORE" ]]; then
  command -v keytool >/dev/null || fail "keytool is required to create the initial test keystore."
  mkdir -p "$(dirname "$KEYSTORE")"
  keytool -genkeypair -noprompt \
    -keystore "$KEYSTORE" -storepass "$STORE_PASS" -keypass "$KEY_PASS" \
    -alias "$KEY_ALIAS" -keyalg RSA -keysize 2048 -validity 10000 \
    -dname "CN=ODPAR Local Build,O=KAOST032,C=EC" >/dev/null 2>&1
fi

rm -rf "$OUT"
mkdir -p "$OUT/apk/lib/arm64-v8a"

COMMON=(
  -std=c11 -O2 -fPIC -ffp-contract=off
  -Wall -Wextra -Werror
  -Wl,--build-id=sha1 -Wl,--no-undefined
)

note "ODPAR Android native build"
note "  SDK:          $SDK"
note "  NDK:          $NDK"
note "  build-tools:  ${BT##*/}"
note "  min API:      $MIN_API"
note "  compile API:  $PLATFORM"

note "[1/6] WhiteLine engine module"
"$CC" "${COMMON[@]}" -fvisibility=hidden -shared \
  -I"$ROOT/engine/include" -I"$ROOT/module/include" \
  "$ROOT/engine/src/odwd_core.c" \
  "$ROOT/engine/src/odwd_simple.c" \
  "$ROOT/engine/src/odwd_render.c" \
  "$ROOT/module/src/whiteline_module.c" \
  -Wl,-soname,libodpar_whiteline.so -lm \
  -o "$OUT/apk/lib/arm64-v8a/libodpar_whiteline.so"

# The module contract deliberately has one public symbol.
exports="$("$TC/llvm-nm" -D --defined-only "$OUT/apk/lib/arm64-v8a/libodpar_whiteline.so" | awk '{print $3}' | sort)"
[[ "$exports" == "odpar_module_get_api" ]] || {
  printf 'Unexpected WhiteLine exports:\n%s\n' "$exports" >&2
  exit 1
}
"$STRIP" --strip-unneeded "$OUT/apk/lib/arm64-v8a/libodpar_whiteline.so"

note "[2/6] NativeActivity shell"
GLUE="$NDK/sources/android/native_app_glue"
[[ -f "$GLUE/android_native_app_glue.c" ]] || fail "native_app_glue missing: $GLUE"
"$CC" "${COMMON[@]}" -shared \
  -I"$GLUE" -I"$ROOT/module/include" \
  "$GLUE/android_native_app_glue.c" \
  "$ROOT/android-shell/src/odpar_shell_android.c" \
  -Wl,-u,ANativeActivity_onCreate -Wl,-soname,libodpar_shell.so \
  -landroid -llog -ldl \
  -o "$OUT/apk/lib/arm64-v8a/libodpar_shell.so"
"$STRIP" --strip-unneeded "$OUT/apk/lib/arm64-v8a/libodpar_shell.so"

note "[3/6] APK skeleton (AAPT2 only; no Gradle, no DEX)"
"$AAPT2" link \
  -I "$ANDROID_JAR" \
  --manifest "$ROOT/android-shell/AndroidManifest.xml" \
  --min-sdk-version "$MIN_API" \
  --target-sdk-version "$PLATFORM" \
  --version-code 700 \
  --version-name "0.7.0-native" \
  -o "$OUT/unsigned-base.apk"

cp "$OUT/unsigned-base.apk" "$OUT/unsigned-with-libs.apk"
(
  cd "$OUT/apk"
  # extractNativeLibs=true allows compressed .so entries; Android installs them
  # into its native library directory before NativeActivity starts.
  zip -q -r "$OUT/unsigned-with-libs.apk" lib
)

note "[4/6] zipalign"
"$ZIPALIGN" -f -p 4 "$OUT/unsigned-with-libs.apk" "$OUT/aligned.apk"

note "[5/6] sign"
"$APKSIGNER" sign \
  --ks "$KEYSTORE" --ks-key-alias "$KEY_ALIAS" \
  --ks-pass "pass:$STORE_PASS" --key-pass "pass:$KEY_PASS" \
  --out "$OUT/ODPAR-WhiteLine-Drift-0.7.0-native-arm64.apk" \
  "$OUT/aligned.apk"

note "[6/6] verify"
"$APKSIGNER" verify --verbose --print-certs \
  "$OUT/ODPAR-WhiteLine-Drift-0.7.0-native-arm64.apk"
unzip -l "$OUT/ODPAR-WhiteLine-Drift-0.7.0-native-arm64.apk" | \
  grep -E 'AndroidManifest.xml|lib/arm64-v8a/libodpar_(shell|whiteline)\.so' >/dev/null

note "APK READY: $OUT/ODPAR-WhiteLine-Drift-0.7.0-native-arm64.apk"
