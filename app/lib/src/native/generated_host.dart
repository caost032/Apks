// GENERATED from host/schema/host_api.json. DO NOT EDIT.
// ignore_for_file: non_constant_identifier_names
import 'dart:ffi' as ffi;

const int odgHostAbiVersion = 1;
const int odgStatusOk = 0;
const int odgStatusInvalidArgument = 1;
const int odgStatusState = 2;
const int odgStatusUnsupported = 3;
const int odgButtonJump = 1;

const int expectedOdgServiceConfigSize = 16;
final class OdgServiceConfig extends ffi.Struct {
  @ffi.Uint32()
  external int struct_size;
  @ffi.Uint32()
  external int abi_version;
  @ffi.Uint32()
  external int render_width;
  @ffi.Uint32()
  external int render_height;
}

const int expectedOdgInputFrameSize = 32;
final class OdgInputFrame extends ffi.Struct {
  @ffi.Uint32()
  external int struct_size;
  @ffi.Uint32()
  external int abi_version;
  @ffi.Uint64()
  external int sequence;
  @ffi.Int16()
  external int move_x_q15;
  @ffi.Int16()
  external int move_forward_q15;
  @ffi.Int16()
  external int look_yaw_q15;
  @ffi.Int16()
  external int look_pitch_q15;
  @ffi.Uint32()
  external int buttons_pressed;
  @ffi.Uint32()
  external int buttons_held;
}

const int expectedOdgUiSnapshotSize = 128;
final class OdgUiSnapshot extends ffi.Struct {
  @ffi.Uint32()
  external int struct_size;
  @ffi.Uint32()
  external int abi_version;
  @ffi.Uint64()
  external int sequence;
  @ffi.Uint64()
  external int simulation_step;
  @ffi.Uint64()
  external int published_ns;
  @ffi.Float()
  external double player_x;
  @ffi.Float()
  external double player_y;
  @ffi.Float()
  external double player_z;
  @ffi.Float()
  external double player_speed;
  @ffi.Float()
  external double player_facing_yaw;
  @ffi.Float()
  external double camera_yaw;
  @ffi.Float()
  external double camera_pitch;
  @ffi.Float()
  external double camera_distance;
  @ffi.Uint32()
  external int grounded;
  @ffi.Uint32()
  external int overload_count;
  @ffi.Uint32()
  external int sim_p50_us;
  @ffi.Uint32()
  external int sim_p95_us;
  @ffi.Uint32()
  external int sim_p99_us;
  @ffi.Uint32()
  external int sim_max_us;
  @ffi.Uint32()
  external int sim_spikes_over_5ms;
  @ffi.Uint32()
  external int render_p50_us;
  @ffi.Uint32()
  external int render_p95_us;
  @ffi.Uint32()
  external int render_p99_us;
  @ffi.Uint32()
  external int render_max_us;
  @ffi.Uint32()
  external int render_spikes_over_16ms;
  @ffi.Uint32()
  external int input_age_us;
  @ffi.Uint32()
  external int render_width;
  @ffi.Uint32()
  external int render_height;
}

const List<String> expectedNativeSymbols = <String>[
  'odg_host_abi_version',
  'odg_service_create',
  'odg_service_destroy',
  'odg_service_start',
  'odg_service_stop',
  'odg_service_submit_input',
  'odg_service_copy_ui_snapshot',
  'odg_service_set_render_extent'
];
