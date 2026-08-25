import 'dart:async';
import 'dart:ffi' as ffi;
import 'dart:isolate';

import 'package:flutter/foundation.dart';

import '../input/game_input_controller.dart';
import '../native/generated_host.dart';
import '../native/odg_bindings.dart';
import 'engine_snapshot.dart';

final class EngineBridge extends ChangeNotifier {
  EngineBridge();

  final ValueNotifier<EngineSnapshot?> snapshot =
      ValueNotifier<EngineSnapshot?>(null);

  Isolate? _worker;
  ReceivePort? _receive;
  SendPort? _commandPort;
  Completer<int>? _readyCompleter;
  Completer<void>? _shutdownCompleter;
  bool _inputInFlight = false;
  int? _nativeHandle;

  int? get nativeHandle => _nativeHandle;
  bool get inputInFlight => _inputInFlight;

  Future<int> start({int renderWidth = 360, int renderHeight = 640}) async {
    if (_worker != null) {
      final int? handle = _nativeHandle;
      if (handle == null) throw StateError('Engine worker is not ready.');
      return handle;
    }
    final ReceivePort receive = ReceivePort('odpar-greenfield-main');
    _receive = receive;
    _readyCompleter = Completer<int>();
    receive.listen(_onWorkerMessage);
    _worker = await Isolate.spawn<Map<String, Object>>(
      _engineWorkerMain,
      <String, Object>{
        'reply': receive.sendPort,
        'width': renderWidth,
        'height': renderHeight,
      },
      debugName: 'odpar-engine-bridge',
      errorsAreFatal: true,
    );
    return _readyCompleter!.future;
  }

  bool trySubmitInput(InputPacket packet) {
    final SendPort? port = _commandPort;
    if (port == null || _inputInFlight) return false;
    _inputInFlight = true;
    port.send(packet.toMessage());
    return true;
  }

  void setRenderExtent(int width, int height) {
    _commandPort?.send(<String, Object>{
      'type': 'extent',
      'width': width,
      'height': height,
    });
  }

  Future<void> shutdown() async {
    final Isolate? worker = _worker;
    if (worker == null) {
      _closeMainPort();
      return;
    }
    final SendPort? port = _commandPort;
    if (port == null) {
      worker.kill(priority: Isolate.immediate);
      _worker = null;
      _closeMainPort();
      return;
    }
    final Completer<void> completer = Completer<void>();
    _shutdownCompleter = completer;
    port.send(<String, Object>{'type': 'shutdown'});
    await completer.future.timeout(
      const Duration(seconds: 2),
      onTimeout: () {
        worker.kill(priority: Isolate.immediate);
      },
    );
    _worker = null;
    _commandPort = null;
    _nativeHandle = null;
    _inputInFlight = false;
    _closeMainPort();
  }

  void _closeMainPort() {
    _receive?.close();
    _receive = null;
  }

  void _onWorkerMessage(Object? raw) {
    if (raw is! Map<Object?, Object?>) return;
    final Object? type = raw['type'];
    if (type == 'ready') {
      _commandPort = raw['commandPort']! as SendPort;
      _nativeHandle = raw['handle']! as int;
      final Completer<int>? ready = _readyCompleter;
      if (ready != null && !ready.isCompleted) ready.complete(_nativeHandle!);
      notifyListeners();
    } else if (type == 'inputAck') {
      _inputInFlight = false;
    } else if (type == 'snapshot') {
      snapshot.value = EngineSnapshot.fromMessage(raw);
    } else if (type == 'fatal') {
      final StateError error = StateError(raw['message']! as String);
      final Completer<int>? ready = _readyCompleter;
      if (ready != null && !ready.isCompleted) ready.completeError(error);
      _inputInFlight = false;
      _commandPort = null;
      _nativeHandle = null;
      _worker = null;
      _closeMainPort();
    } else if (type == 'stopped') {
      final Completer<void>? stopped = _shutdownCompleter;
      if (stopped != null && !stopped.isCompleted) stopped.complete();
    }
  }

  @override
  void dispose() {
    snapshot.dispose();
    super.dispose();
  }
}

Future<void> _engineWorkerMain(Map<String, Object> bootstrap) async {
  final SendPort reply = bootstrap['reply']! as SendPort;
  final int width = bootstrap['width']! as int;
  final int height = bootstrap['height']! as int;
  final ReceivePort commands = ReceivePort('odpar-greenfield-engine-commands');
  OdgBindings? bindings;
  ffi.Pointer<OdgEngineService>? service;
  Timer? snapshotTimer;
  bool started = false;
  try {
    bindings = OdgBindings.open();
    final ffi.Pointer<OdgEngineService> created = bindings.create(
      renderWidth: width,
      renderHeight: height,
    );
    if (created == ffi.nullptr) {
      throw StateError('Native engine service allocation failed.');
    }
    service = created;
    if (bindings.start(created) != odgStatusOk) {
      throw StateError('Native engine service failed to start.');
    }
    started = true;
    reply.send(<String, Object>{
      'type': 'ready',
      'commandPort': commands.sendPort,
      'handle': created.address,
    });

    snapshotTimer = Timer.periodic(const Duration(milliseconds: 50), (_) {
      final EngineSnapshot? snapshot = bindings!.copySnapshot(created);
      if (snapshot != null) reply.send(snapshot.toMessage());
    });

    await for (final Object? raw in commands) {
      if (raw is! Map<Object?, Object?>) continue;
      final Object? type = raw['type'];
      if (type == 'input') {
        bindings.submit(
          created,
          sequence: raw['sequence']! as int,
          moveXQ15: raw['moveXQ15']! as int,
          moveForwardQ15: raw['moveForwardQ15']! as int,
          lookYawQ15: raw['lookYawQ15']! as int,
          lookPitchQ15: raw['lookPitchQ15']! as int,
          buttonsPressed: raw['buttonsPressed']! as int,
          buttonsHeld: raw['buttonsHeld']! as int,
        );
        reply.send(<String, Object>{'type': 'inputAck'});
      } else if (type == 'extent') {
        bindings.setRenderExtent(
          created,
          raw['width']! as int,
          raw['height']! as int,
        );
      } else if (type == 'shutdown') {
        break;
      }
    }
  } catch (error) {
    reply.send(<String, Object>{
      'type': 'fatal',
      'message': error.toString(),
    });
  } finally {
    snapshotTimer?.cancel();
    final OdgBindings? native = bindings;
    final ffi.Pointer<OdgEngineService>? owned = service;
    if (native != null && owned != null) {
      if (started) native.stop(owned);
      native.destroy(owned);
    }
    commands.close();
    if (service != null) {
      reply.send(<String, Object>{'type': 'stopped'});
    }
  }
}
