# ODPAR: WhiteLine Drift — Native Rebuild

Este árbol es la reconstrucción nativa avanzada de WhiteLine Drift a partir del checkpoint recuperado v0.6.1 / v0.6.2-WIP.

## Regla de arquitectura

- WhiteLine Drift conserva **su propio motor C**.
- ODPAR es el contenedor/launcher; no es el motor de WhiteLine.
- El gameplay normal no depende de Flutter, Dart, WebView ni WASM.
- El módulo jugable exporta un único símbolo: `odpar_module_get_api`.
- La APK de prueba usa `NativeActivity` y una ruta de build **sin Gradle**.
- Cambiar física, IA, renderer, cámaras o gameplay de WhiteLine no debe requerir rehacer la arquitectura Android.

## Árbol

- `engine/` — motor C recuperado y mejorado.
- `module/` — adaptador mínimo WhiteLine -> contrato ODPAR.
- `android-shell/` — shell NativeActivity mínimo para probar la ruta Android.
- `assets/` — assets conservados del proyecto.
- `tools/test_native.sh` — gate local nativo.
- `tools/build_android_apk_no_gradle.sh` — build APK directo con SDK/NDK oficiales, sin Gradle.

## Gate actual

`./tools/test_native.sh` verifica:

1. core C;
2. invariantes Survival/Music Survival;
3. regresiones recuperadas de AUTO/flood/BlockDash;
4. renderer;
5. módulo compartido con exactamente un export público;
6. carga real con `dlopen`;
7. lifecycle pause/resume;
8. BACK/pause;
9. los cuatro modos: ENDLESS, OPEN, BlockDash y Music Survival;
10. sintaxis del build Android sin Gradle.

El gate nativo está verde en este checkpoint.

## APK Android

La ruta Android está preparada, pero este contenedor de trabajo no incluye Android SDK/NDK, por lo que aquí no se afirma falsamente que el APK Android haya sido compilado. En un entorno que tenga SDK/NDK:

```bash
./tools/build_android_apk_no_gradle.sh
```

La meta de ese script es deliberadamente pequeña: Clang/NDK -> dos `.so` -> AAPT2 -> zipalign -> apksigner. No Flutter. No Dart. No Gradle.
