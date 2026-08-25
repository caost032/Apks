import 'dart:ffi' as ffi;

import 'package:ffi/ffi.dart';

import '../engine/engine_snapshot.dart';
import 'generated_host.dart';

final class OdgEngineService extends ffi.Opaque {}

typedef _AbiNative = ffi.Uint32 Function();
typedef _AbiDart = int Function();
typedef _CreateNative = ffi.Pointer<OdgEngineService> Function(
  ffi.Pointer<OdgServiceConfig>,
);
typedef _CreateDart = ffi.Pointer<OdgEngineService> Function(
  ffi.Pointer<OdgServiceConfig>,
);
typedef _DestroyNative = ffi.Void Function(ffi.Pointer<OdgEngineService>);
typedef _DestroyDart = void Function(ffi.Pointer<OdgEngineService>);
typedef _StartNative = ffi.Uint32 Function(ffi.Pointer<OdgEngineService>);
typedef _StartDart = int Function(ffi.Pointer<OdgEngineService>);
typedef _StopNative = ffi.Void Function(ffi.Pointer<OdgEngineService>);
typedef _StopDart = void Function(ffi.Pointer<OdgEngineService>);
typedef _SubmitNative = ffi.Uint32 Function(
  ffi.Pointer<OdgEngineService>,
  ffi.Pointer<OdgInputFrame>,
);
typedef _SubmitDart = int Function(
  ffi.Pointer<OdgEngineService>,
  ffi.Pointer<OdgInputFrame>,
);
typedef _CopySnapshotNative = ffi.Uint32 Function(
  ffi.Pointer<OdgEngineService>,
  ffi.Pointer<OdgUiSnapshot>,
);
typedef _CopySnapshotDart = int Function(
  ffi.Pointer<OdgEngineService>,
  ffi.Pointer<OdgUiSnapshot>,
);
typedef _SetExtentNative = ffi.Uint32 Function(
  ffi.Pointer<OdgEngineService>,
  ffi.Uint32,
  ffi.Uint32,
);
typedef _SetExtentDart = int Function(
  ffi.Pointer<OdgEngineService>,
  int,
  int,
);

final class OdgBindings {
  OdgBindings._(ffi.DynamicLibrary library)
      : _abi = library.lookupFunction<_AbiNative, _AbiDart>(
          'odg_host_abi_version',
        ),
        _create = library.lookupFunction<_CreateNative, _CreateDart>(
          'odg_service_create',
        ),
        _destroy = library.lookupFunction<_DestroyNative, _DestroyDart>(
          'odg_service_destroy',
        ),
        _start = library.lookupFunction<_StartNative, _StartDart>(
          'odg_service_start',
        ),
        _stop = library.lookupFunction<_StopNative, _StopDart>(
          'odg_service_stop',
        ),
        _submit = library.lookupFunction<_SubmitNative, _SubmitDart>(
          'odg_service_submit_input',
        ),
        _copySnapshot =
            library.lookupFunction<_CopySnapshotNative, _CopySnapshotDart>(
          'odg_service_copy_ui_snapshot',
        ),
        _setExtent = library.lookupFunction<_SetExtentNative, _SetExtentDart>(
          'odg_service_set_render_extent',
        );

  factory OdgBindings.open() {
    final ffi.DynamicLibrary library = ffi.DynamicLibrary.open(
      'libodpar_greenfield.so',
    );
    final OdgBindings bindings = OdgBindings._(library);
    final int nativeAbi = bindings._abi();
    if (nativeAbi != odgHostAbiVersion) {
      throw StateError(
        'ODPAR host ABI mismatch: Dart=$odgHostAbiVersion native=$nativeAbi',
      );
    }
    return bindings;
  }

  final _AbiDart _abi;
  final _CreateDart _create;
  final _DestroyDart _destroy;
  final _StartDart _start;
  final _StopDart _stop;
  final _SubmitDart _submit;
  final _CopySnapshotDart _copySnapshot;
  final _SetExtentDart _setExtent;

  ffi.Pointer<OdgEngineService> create({
    required int renderWidth,
    required int renderHeight,
  }) {
    final ffi.Pointer<OdgServiceConfig> config = calloc<OdgServiceConfig>();
    try {
      config.ref
        ..struct_size = expectedOdgServiceConfigSize
        ..abi_version = odgHostAbiVersion
        ..render_width = renderWidth
        ..render_height = renderHeight;
      return _create(config);
    } finally {
      calloc.free(config);
    }
  }

  int start(ffi.Pointer<OdgEngineService> service) => _start(service);

  void stop(ffi.Pointer<OdgEngineService> service) => _stop(service);

  void destroy(ffi.Pointer<OdgEngineService> service) => _destroy(service);

  int setRenderExtent(
    ffi.Pointer<OdgEngineService> service,
    int width,
    int height,
  ) =>
      _setExtent(service, width, height);

  int submit(
    ffi.Pointer<OdgEngineService> service, {
    required int sequence,
    required int moveXQ15,
    required int moveForwardQ15,
    required int lookYawQ15,
    required int lookPitchQ15,
    required int buttonsPressed,
    required int buttonsHeld,
  }) {
    final ffi.Pointer<OdgInputFrame> frame = calloc<OdgInputFrame>();
    try {
      frame.ref
        ..struct_size = expectedOdgInputFrameSize
        ..abi_version = odgHostAbiVersion
        ..sequence = sequence
        ..move_x_q15 = moveXQ15
        ..move_forward_q15 = moveForwardQ15
        ..look_yaw_q15 = lookYawQ15
        ..look_pitch_q15 = lookPitchQ15
        ..buttons_pressed = buttonsPressed
        ..buttons_held = buttonsHeld;
      return _submit(service, frame);
    } finally {
      calloc.free(frame);
    }
  }

  EngineSnapshot? copySnapshot(ffi.Pointer<OdgEngineService> service) {
    final ffi.Pointer<OdgUiSnapshot> snapshot = calloc<OdgUiSnapshot>();
    try {
      snapshot.ref
        ..struct_size = expectedOdgUiSnapshotSize
        ..abi_version = odgHostAbiVersion;
      if (_copySnapshot(service, snapshot) != odgStatusOk) {
        return null;
      }
      final OdgUiSnapshot s = snapshot.ref;
      return EngineSnapshot(
        sequence: s.sequence,
        simulationStep: s.simulation_step,
        publishedNs: s.published_ns,
        playerX: s.player_x,
        playerY: s.player_y,
        playerZ: s.player_z,
        playerSpeed: s.player_speed,
        playerFacingYaw: s.player_facing_yaw,
        cameraYaw: s.camera_yaw,
        cameraPitch: s.camera_pitch,
        cameraDistance: s.camera_distance,
        grounded: s.grounded != 0,
        overloadCount: s.overload_count,
        simP50Us: s.sim_p50_us,
        simP95Us: s.sim_p95_us,
        simP99Us: s.sim_p99_us,
        simMaxUs: s.sim_max_us,
        simSpikesOver5Ms: s.sim_spikes_over_5ms,
        renderP50Us: s.render_p50_us,
        renderP95Us: s.render_p95_us,
        renderP99Us: s.render_p99_us,
        renderMaxUs: s.render_max_us,
        renderSpikesOver16Ms: s.render_spikes_over_16ms,
        inputAgeUs: s.input_age_us,
        renderWidth: s.render_width,
        renderHeight: s.render_height,
      );
    } finally {
      calloc.free(snapshot);
    }
  }
}
