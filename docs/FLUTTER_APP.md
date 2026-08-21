# Aplicación Flutter/Dart FFI

`app/flutter/` es la aplicación principal de ODPAR: Territorial Domain. Flutter presenta, enruta
tacto y administra ciclo de vida/archivos; reglas, simulación y rasterización permanecen en C11.
No hay WebView ni una segunda implementación del gameplay en Dart.

## Ruta de datos

```text
multitouch Flutter -> Q15/input + comandos -> C11 fijo 120 Hz
C11 render -> RGBA8 -> copia segura -> ui.Image -> RawImage
C11 snapshots/queries paginadas -> modelos de presentación Dart -> UI
```

El host consulta `odg_ffi_abi_query()` antes de inicializar. El contrato actual es:

| Contrato | Valor actual |
| --- | ---: |
| Engine API | 37 |
| FFI ABI | 9 |
| Save writer | schema 25 |
| `odg_ffi_abi_info` | 112 bytes |
| Pixel | RGBA8, 4 bytes |
| Simulación | 120 Hz fijo |

ABI9 valida `odg_construction_snapshot` y el contrato paginado de artifacts con `total_count` explícito; la construcción expone forma seleccionada y salud estructural. Dart usa `sizeOf<T>()`; no conserva punteros mutables del framebuffer C durante la
conversión asíncrona de imagen.

## Presentación y controles

- portrait/landscape usan `SafeArea` y perfiles normalizados;
- joystick, free-look y botones tienen ownership multitouch independiente;
- pausa/pérdida de foco neutraliza input;
- HUD, mapa, construcción, inventario y crafting solo leen snapshots/queries;
- al seleccionar un bloque aparece selector SUELO / MURO / VANO / TECHO;
- `GOLPEAR`, `REPARAR`, `COLOCAR` y `DESMONTAR` provienen del hint nativo;
- calidad raster puede cambiar sin alterar frecuencia, mundo, bots o precisión.

## Estructura

```text
app/flutter/
  CMakeLists.txt                  mismo C11 -> biblioteca dinámica
  lib/src/native/                ABI 9, POD, layouts y negociación
  lib/src/engine/                reloj, snapshots, paginación, framebuffer
  lib/src/input/                 multitouch y Q15
  lib/src/render/                presupuesto raster adaptativo
  lib/src/ui/                    juego, HUD, mundos, construcción y tema
  test/                          ABI, input y raster
  android/                       host Flutter mínimo
```

## Desarrollo Flutter

```sh
cd app/flutter
flutter pub get
dart format --output=none --set-exit-if-changed lib test
flutter analyze
flutter test
```

## APK

Gradle/CMake compila directamente el núcleo autoritativo con stack protector, RELRO/NOW, pila no
ejecutable, `--no-undefined`, páginas Android compatibles y el version-script público. ABI objetivo:
`arm64-v8a`, `armeabi-v7a`, `x86_64`.

```sh
cd app/flutter
flutter pub get
flutter build apk --release --target-platform android-arm,android-arm64,android-x64
flutter build apk --release --split-per-abi --target-platform android-arm,android-arm64,android-x64
```

`.github/workflows/android.yml` deriva la API desde `odpar_game.h`. Una publicación real debe usar
upload key protegida mediante secrets.

## Validación local de este checkpoint

La prueba nativa actual confirma FFI ABI 9 / API 37 y portrait `720x1280` (921 600 píxeles). Este
entorno no afirma APK local si no dispone del toolchain completo Flutter/Android; el contrato host se
valida estáticamente y la compilación APK corresponde al toolchain objetivo/CI.
