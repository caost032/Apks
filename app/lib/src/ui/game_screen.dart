import 'dart:async';
import 'dart:ui' show FontFeature;

import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';

import '../engine/engine_bridge.dart';
import '../engine/frame_pacing.dart';
import '../engine/render_extent.dart';
import '../input/game_input_controller.dart';
import '../engine/engine_snapshot.dart';
import '../platform/native_texture.dart';

class GameScreen extends StatefulWidget {
  const GameScreen({super.key});

  @override
  State<GameScreen> createState() => _GameScreenState();
}

class _GameScreenState extends State<GameScreen>
    with SingleTickerProviderStateMixin, WidgetsBindingObserver {
  final EngineBridge _engine = EngineBridge();
  final GameInputController _input = GameInputController();
  final FramePacingTracker _frames = FramePacingTracker();
  late final Ticker _inputTicker;
  late final Future<void> _startupFuture;
  int? _textureId;
  bool _starting = true;
  String? _failure;
  Size _viewport = Size.zero;
  Size _renderExtent = const Size(360, 640);

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _inputTicker = createTicker(_sampleInput)..start();
    _startupFuture = _startEngine();
    unawaited(_startupFuture);
  }

  Future<void> _startEngine() async {
    try {
      final int handle = await _engine.start(
        renderWidth: _renderExtent.width.round(),
        renderHeight: _renderExtent.height.round(),
      );
      if (!mounted) return;
      final int textureId = await NativeTextureHost.create(
        serviceHandle: handle,
        width: _renderExtent.width.round(),
        height: _renderExtent.height.round(),
      );
      if (!mounted) {
        await NativeTextureHost.release(textureId);
        return;
      }
      setState(() {
        _textureId = textureId;
        _starting = false;
      });
    } catch (error) {
      if (!mounted) return;
      setState(() {
        _failure = error.toString();
        _starting = false;
      });
    }
  }

  void _sampleInput(Duration _) {
    if (_engine.inputInFlight) return;
    final InputPacket packet = _input.sampleForEngine();
    if (!_engine.trySubmitInput(packet)) {
      _input.restoreUnsentLook(packet);
    }
  }

  void _onViewport(Size viewport) {
    if (viewport == _viewport) return;
    _viewport = viewport;
    final Size next = chooseNativeRenderExtent(viewport);
    if (next == _renderExtent) return;
    _renderExtent = next;
    _engine.setRenderExtent(next.width.round(), next.height.round());
    final int? textureId = _textureId;
    if (textureId != null) {
      unawaited(
        NativeTextureHost.resize(
          textureId: textureId,
          width: next.width.round(),
          height: next.height.round(),
        ),
      );
    }
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.paused ||
        state == AppLifecycleState.inactive) {
      _input.cancelActiveGestures();
      _sampleInput(Duration.zero);
    }
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _inputTicker.dispose();
    _input.dispose();
    _frames.dispose();
    unawaited(_shutdownProduct());
    super.dispose();
  }

  Future<void> _shutdownProduct() async {
    // Serialize teardown after asynchronous startup so the Android texture can
    // never retain a native handle concurrently with Dart destroying it.
    await _startupFuture;
    final int? textureId = _textureId;
    if (textureId != null) {
      try {
        await NativeTextureHost.release(textureId);
      } catch (_) {
        // Android teardown may already have removed the surface.
      }
    }
    await _engine.shutdown();
    _engine.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF0C1014),
      body: LayoutBuilder(
        builder: (BuildContext context, BoxConstraints constraints) {
          final Size viewport = constraints.biggest;
          WidgetsBinding.instance.addPostFrameCallback((_) {
            if (mounted) _onViewport(viewport);
          });
          return Listener(
            behavior: HitTestBehavior.opaque,
            onPointerDown: (PointerDownEvent event) =>
                _input.pointerDown(event, viewport),
            onPointerMove: (PointerMoveEvent event) =>
                _input.pointerMove(event, viewport),
            onPointerUp: (PointerUpEvent event) =>
                _input.pointerUpOrCancel(event.pointer),
            onPointerCancel: (PointerCancelEvent event) =>
                _input.pointerUpOrCancel(event.pointer),
            child: Stack(
              fit: StackFit.expand,
              children: <Widget>[
                _GameTexture(textureId: _textureId),
                const IgnorePointer(child: _Reticle()),
                AnimatedBuilder(
                  animation: _input,
                  builder: (BuildContext context, Widget? child) =>
                      _MovementStick(value: _input.movement),
                ),
                _JumpButton(
                  onDown: _input.pressJump,
                  onUp: _input.releaseJump,
                ),
                ValueListenableBuilder<EngineSnapshot?>(
                  valueListenable: _engine.snapshot,
                  builder: (BuildContext context, EngineSnapshot? snapshot,
                          Widget? child) =>
                      AnimatedBuilder(
                    animation: _frames,
                    builder: (BuildContext context, Widget? child) => _Telemetry(
                      snapshot: snapshot,
                      framePacing: _frames.value,
                    ),
                  ),
                ),
                SafeArea(
                  child: Align(
                    alignment: Alignment.topRight,
                    child: Padding(
                      padding: const EdgeInsets.all(10),
                      child: AnimatedBuilder(
                        animation: _input,
                        builder: (BuildContext context, Widget? child) =>
                            _InvertToggle(
                          value: _input.invertY,
                          onChanged: _input.setInvertY,
                        ),
                      ),
                    ),
                  ),
                ),
                if (_starting || _failure != null)
                  _StartupOverlay(failure: _failure),
              ],
            ),
          );
        },
      ),
    );
  }
}

class _GameTexture extends StatelessWidget {
  const _GameTexture({required this.textureId});
  final int? textureId;

  @override
  Widget build(BuildContext context) {
    final int? id = textureId;
    if (id == null) return const ColoredBox(color: Color(0xFF0C1014));
    return Texture(textureId: id, filterQuality: FilterQuality.low);
  }
}

class _Reticle extends StatelessWidget {
  const _Reticle();

  @override
  Widget build(BuildContext context) {
    return Center(
      child: SizedBox(
        key: const ValueKey<String>('reticle'),
        width: 20,
        height: 20,
        child: CustomPaint(painter: _ReticlePainter()),
      ),
    );
  }
}

class _ReticlePainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final Paint shadow = Paint()
      ..color = const Color(0xB0000000)
      ..strokeWidth = 3.0;
    final Paint line = Paint()
      ..color = const Color(0xF2FFFFFF)
      ..strokeWidth = 1.25;
    final Offset c = size.center(Offset.zero);
    final List<List<Offset>> strokes = <List<Offset>>[
      <Offset>[Offset(c.dx, 1), Offset(c.dx, 6)],
      <Offset>[Offset(c.dx, size.height - 1), Offset(c.dx, size.height - 6)],
      <Offset>[Offset(1, c.dy), Offset(6, c.dy)],
      <Offset>[Offset(size.width - 1, c.dy), Offset(size.width - 6, c.dy)],
    ];
    for (final List<Offset> stroke in strokes) {
      canvas.drawLine(stroke[0], stroke[1], shadow);
      canvas.drawLine(stroke[0], stroke[1], line);
    }
  }

  @override
  bool shouldRepaint(covariant _ReticlePainter oldDelegate) => false;
}

class _MovementStick extends StatelessWidget {
  const _MovementStick({required this.value});
  final Offset value;

  @override
  Widget build(BuildContext context) {
    return SafeArea(
      child: Align(
        alignment: Alignment.bottomLeft,
        child: Padding(
          padding: const EdgeInsets.fromLTRB(22, 0, 0, 24),
          child: IgnorePointer(
            child: SizedBox(
              key: const ValueKey<String>('movement-stick'),
              width: 124,
              height: 124,
              child: Stack(
                alignment: Alignment.center,
                children: <Widget>[
                  Container(
                    width: 116,
                    height: 116,
                    decoration: BoxDecoration(
                      shape: BoxShape.circle,
                      border: Border.all(color: const Color(0x52FFFFFF)),
                      color: const Color(0x19000000),
                    ),
                  ),
                  Transform.translate(
                    offset: Offset(value.dx * 34, -value.dy * 34),
                    child: Container(
                      width: 48,
                      height: 48,
                      decoration: const BoxDecoration(
                        shape: BoxShape.circle,
                        color: Color(0xA8FFFFFF),
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class _JumpButton extends StatelessWidget {
  const _JumpButton({required this.onDown, required this.onUp});
  final VoidCallback onDown;
  final VoidCallback onUp;

  @override
  Widget build(BuildContext context) {
    return SafeArea(
      child: Align(
        alignment: Alignment.bottomRight,
        child: Padding(
          padding: const EdgeInsets.fromLTRB(0, 0, 22, 30),
          child: Listener(
            onPointerDown: (_) => onDown(),
            onPointerUp: (_) => onUp(),
            onPointerCancel: (_) => onUp(),
            child: Semantics(
              button: true,
              label: 'Saltar',
              child: Container(
                key: const ValueKey<String>('jump-button'),
                width: 68,
                height: 68,
                alignment: Alignment.center,
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  color: const Color(0xA8182028),
                  border: Border.all(color: const Color(0xA6FFFFFF)),
                ),
                child: const Icon(
                  Icons.arrow_upward_rounded,
                  color: Colors.white,
                  size: 30,
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class _Telemetry extends StatelessWidget {
  const _Telemetry({required this.snapshot, required this.framePacing});
  final EngineSnapshot? snapshot;
  final FramePacingSnapshot framePacing;

  @override
  Widget build(BuildContext context) {
    final EngineSnapshot? s = snapshot;
    return SafeArea(
      child: Align(
        alignment: Alignment.topLeft,
        child: Padding(
          padding: const EdgeInsets.all(10),
          child: DecoratedBox(
            decoration: BoxDecoration(
              color: const Color(0x8F091017),
              borderRadius: BorderRadius.circular(8),
              border: Border.all(color: const Color(0x28FFFFFF)),
            ),
            child: Padding(
              padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 7),
              child: Text(
                s == null
                    ? 'FRAME ${framePacing.p50Us}/${framePacing.p95Us}/${framePacing.p99Us} µs  MAX ${framePacing.maxUs}'
                    : 'FRM ${framePacing.p50Us}/${framePacing.p95Us}/${framePacing.p99Us}  MAX ${framePacing.maxUs} µs\n'
                        'SIM ${s.simP50Us}/${s.simP95Us}/${s.simP99Us}  MAX ${s.simMaxUs}  >5 ${s.simSpikesOver5Ms}\n'
                        'REN ${s.renderP50Us}/${s.renderP95Us}/${s.renderP99Us}  MAX ${s.renderMaxUs}  >16 ${s.renderSpikesOver16Ms}\n'
                        'FRM>50 ${framePacing.spikesOver50Ms}  OV ${s.overloadCount}  IN ${s.inputAgeUs} µs',
                key: const ValueKey<String>('telemetry'),
                style: const TextStyle(
                  color: Color(0xE6FFFFFF),
                  fontSize: 10,
                  height: 1.35,
                  fontFeatures: <FontFeature>[FontFeature.tabularFigures()],
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class _InvertToggle extends StatelessWidget {
  const _InvertToggle({required this.value, required this.onChanged});
  final bool value;
  final ValueChanged<bool> onChanged;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      label: 'Invertir eje vertical',
      toggled: value,
      child: InkWell(
        onTap: () => onChanged(!value),
        borderRadius: BorderRadius.circular(8),
        child: Container(
          key: const ValueKey<String>('invert-y-toggle'),
          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
          decoration: BoxDecoration(
            color: value ? const Color(0xB7375568) : const Color(0x8F091017),
            borderRadius: BorderRadius.circular(8),
            border: Border.all(color: const Color(0x32FFFFFF)),
          ),
          child: Text(
            value ? 'Y INV' : 'Y NAT',
            style: const TextStyle(
              color: Colors.white,
              fontSize: 10,
              fontWeight: FontWeight.w700,
            ),
          ),
        ),
      ),
    );
  }
}

class _StartupOverlay extends StatelessWidget {
  const _StartupOverlay({required this.failure});
  final String? failure;

  @override
  Widget build(BuildContext context) {
    final String? message = failure;
    return ColoredBox(
      color: const Color(0xC80C1014),
      child: Center(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: message == null
              ? const CircularProgressIndicator(strokeWidth: 2)
              : Text(
                  'GREENFIELD ENGINE ERROR\n$message',
                  textAlign: TextAlign.center,
                  style: const TextStyle(color: Colors.white),
                ),
        ),
      ),
    );
  }
}
