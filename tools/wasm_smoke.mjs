import fs from 'node:fs';

function cAuthorityApiVersion() {
  const header = fs.readFileSync(
    new URL('../engine/include/odpar_game.h', import.meta.url),
    'utf8',
  );
  const match = header.match(/^\s*#define\s+ODG_API_VERSION\s+UINT32_C\((\d+)\)\s*$/m);
  if (!match) throw new Error('cannot derive ODG_API_VERSION from C authority');
  return Number(match[1]);
}

const expectedApi = cAuthorityApiVersion();
const bytes = fs.readFileSync(new URL('../build/odpar_territorial_domain.wasm', import.meta.url));
const { instance } = await WebAssembly.instantiate(bytes, {});
const e = instance.exports;

const actualApi = e.odg_api_version();
if (actualApi !== expectedApi) throw new Error(`bad api ${actualApi}; expected ${expectedApi}`);
if (e.odg_init(0x1234567890abcdn, 480, 270) !== 0) throw new Error('init failed');

const required = [
  'odg_ffi_abi_query', 'odg_framebuffer_stride_bytes', 'odg_copy_framebuffer',
  'odg_stats_ptr', 'odg_copy_stats',
  'odg_territory_total_cells', 'odg_player_territory_cells', 'odg_player_territory_permille',
  'odg_player_trail_cells', 'odg_player_trail_active', 'odg_turret_count',
  'odg_player_owned_turrets', 'odg_player_carrying_turret', 'odg_player_carried_turret_ammo',
  'odg_player_turret_action_available', 'odg_ammo_crate_count', 'odg_player_carrying_ammo_crate', 'odg_player_carried_ammo',
  'odg_player_facing_x_q15', 'odg_player_facing_z_q15', 'odg_camera_dir_x_q15', 'odg_camera_dir_z_q15',
  'odg_control_heading_x_q15', 'odg_control_heading_z_q15', 'odg_control_local_x_q15', 'odg_control_local_z_q15', 'odg_control_strength_q15',
  'odg_set_world_input', 'odg_set_visual_theme', 'odg_visual_theme', 'odg_set_presentation_mode', 'odg_presentation_mode',
  'odg_match_over', 'odg_winner_id', 'odg_player_death_reason',
  'odg_chip_count', 'odg_player_carrying_chip', 'odg_player_hack_action_available', 'odg_player_drop_action_available',
  'odg_player_nearby_owned_turret_visible', 'odg_player_nearby_owned_turret_ammo', 'odg_player_nearby_owned_turret_max_ammo',
  'odg_copy_resources', 'odg_copy_fauna', 'odg_copy_fauna_nests',
  'odg_construction_count', 'odg_copy_construction_page',
  'odg_food_definition_count', 'odg_flora_species_count', 'odg_fauna_species_count', 'odg_fauna_habitat_count',
  'odg_fauna_nesting_count', 'odg_fauna_nesting_get',
  'odg_world_surface_sample64', 'odg_world_geology_material64', 'odg_world_geology_ore_resource64',
  'odg_world_cave_openness_permille64', 'odg_world_cave_entrance64', 'odg_worldgen_version',
  'odg_day_index', 'odg_day_phase_permille', 'odg_daylight_permille', 'odg_is_night',
  'odg_player_satiety_permille', 'odg_player_hydration_permille', 'odg_player_oxygen_permille',
  'odg_player_trail_broken', 'odg_weather_rain_permille'
];
for (const name of required) if (typeof e[name] !== 'function') throw new Error(`missing export ${name}`);

e.odg_set_world_input(32767, 0, 30000, 0, 0, 0);
e.odg_step_ticks(24);
for (let i = 0; i < 1000 && e.odg_match_over() === 0; i++) {
  const phase = Math.floor(i / 200) % 4;
  const mx = phase === 1 ? 25000 : phase === 3 ? -25000 : 0;
  const my = phase === 0 ? 30000 : phase === 2 ? -30000 : 0;
  e.odg_set_input(mx, my, 0, 0, 0);
  e.odg_step_ticks(1);
}

const ptr = e.odg_render_frame();
const len = e.odg_framebuffer_bytes();
const view = new Uint8Array(e.memory.buffer, ptr, len);
let hash = 2166136261 >>> 0;
for (let i = 0; i < view.length; i += 97) { hash ^= view[i]; hash = Math.imul(hash, 16777619) >>> 0; }
const total = e.odg_territory_total_cells();
const permille = e.odg_player_territory_permille();
const alive = e.odg_alive_count();
if (!ptr || len !== 480 * 270 * 4 || hash === 0) throw new Error('bad frame');
if (!(total > 0 && total < 16384)) throw new Error(`bad claimed territory ${total}`);
if (!(e.odg_turret_count() > 0 && e.odg_turret_count() < 128)) throw new Error(`bad turret population ${e.odg_turret_count()}`);
if (permille > 1000 || alive > 10) throw new Error('bad gameplay stats');

e.odg_set_visual_theme(3);
if (e.odg_visual_theme() !== 3) throw new Error('theme api failed');
const gameplayHash=e.odg_state_hash();
e.odg_set_presentation_mode(1);
if (e.odg_presentation_mode() !== 1 || e.odg_state_hash() !== gameplayHash) throw new Error('presentation api failed');
e.odg_set_presentation_mode(0);
console.log(`WASM OK api=${expectedApi} bytes=${bytes.length} framebuffer=${len} sampleHash=${hash.toString(16)} claimed=${total} territory=${(permille/10).toFixed(1)}% alive=${alive} turrets=${e.odg_turret_count()}`);
