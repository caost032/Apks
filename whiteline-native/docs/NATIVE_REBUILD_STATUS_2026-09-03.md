# WhiteLine Drift — Native Rebuild Status — 2026-09-03

## Baseline recuperado

El archivo de recuperación se llamaba `v0.6.2-WIP`, pero su release durable interna era v0.6.1. La nota WIP documentaba mejoras post-v0.6.1 que habían sido medidas antes de perderse el árbol temporal.

## Trabajo reconstruido y conservado

- Eliminada la dependencia arquitectónica Flutter/Dart/FFI para el nuevo runtime.
- Nuevo contrato `ODPAR_MODULE_ABI 1` pequeño.
- WhiteLine compila como módulo compartido independiente.
- Superficie pública del módulo: **1 símbolo** (`odpar_module_get_api`).
- Prueba host real con `dlopen`/`dlsym`/`dlclose`.
- Menú, controles táctiles, cámara, AUTO, pause/resume y BACK cruzan el contrato nativo.
- Los cuatro modos cargan, avanzan y renderizan mediante el mismo módulo.
- NativeActivity shell separado del motor del juego.
- Build APK diseñado sin Gradle ni DEX.
- Script Android con autodetección de SDK/NDK/build-tools instalados y keystore de prueba persistente.

## Recuperación WIP incorporada

Se preservaron y volvieron permanentes los puntos importantes documentados en `WIP_RECOVERY_2026-08-21.md`:

- Music AUTO puntúa recorrido, no solo destino.
- Flood tiene exactamente un refugio ganador.
- Refugio flood ganador queda aproximadamente a 26–43 m del jugador.
- Warning flood se escala aproximadamente 3.55–4.65 s.
- AUTO desacelera al aproximarse y se asienta en refugio.
- BlockDash excluye SLALOM del sector 0, no de sectores posteriores.
- Player AUTO de BlockDash está separado del bot controller.
- Semillas 29, 31 y 48 tienen gate de supervivencia mínima de 18 s.
- Music AUTO tiene un gate energético de 12 semillas/45 s: al menos 10 sobreviven todo el intervalo y ninguna derrota permitida antes de 20 s.

## Medición diagnóstica tras fusionar el core más reciente

Stress auxiliar de 48 semillas (no forma parte del gate rápido completo):

- Music AUTO: 44/48 sobreviven 60 s; mínimo observado ~32.99 s; media ~58.78 s.
- BlockDash AUTO: 5/48 sobreviven 45 s; media ~28.79 s.
- BlockDash seed 29: ~28.86 s.
- BlockDash seed 31: ~28.53 s.
- BlockDash seed 48: 45 s.

Estas cifras sirven para detectar regresiones; no son promesas de dificultad final.

## Correcciones del host

- `PAUSE/RESUME` del lifecycle ya no comparte el mismo flag con la pausa del juego. Antes un background/resume podía dejar el módulo congelado.
- BACK se captura como key event en el NativeActivity shell y se envía al módulo.
- Los tests se construyen en un directorio temporal escribible para evitar bloqueos por ownership de artefactos anteriores.
- El gate compila los translation units comunes una sola vez, reduciendo recompilaciones innecesarias.

## Verificado en este entorno

PASS:

- C11 core
- survival invariants
- recovered AUTO regressions
- renderer regression hash
- single-symbol module ABI
- dynamic module loading
- all four game modes
- lifecycle pause/resume
- BACK semantics at module level
- Android build-script shell syntax

## Pendiente de verificación física

No hay Android SDK/NDK instalado en el contenedor actual. Por ello sigue pendiente ejecutar el build real de APK y probarlo en el teléfono objetivo.

Eso es un gate de plataforma, no una razón para volver a introducir Flutter/Gradle dentro del juego.
