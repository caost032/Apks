SHELL := /bin/sh
CC ?= cc
CLANG ?= clang
BUILD := build
INCLUDES := -Iengine/include -Iengine/src -Iengine/vendor
WARN := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Werror
NATIVE_HARDEN := -fstack-protector-strong -D_FORTIFY_SOURCE=2
NATIVE_LINK := -Wl,--version-script=engine/odpar_territorial_domain.exports.map -Wl,-z,relro,-z,now,-z,noexecstack
SANITIZER_OPTIONS ?= detect_leaks=1
ENGINE := engine/src/platform.c engine/src/entities.c engine/src/game.c engine/src/world_physics.c engine/src/items.c engine/src/content_registry.c engine/src/territory_policy.c engine/src/environment.c engine/src/geology.c engine/src/survival.c engine/src/fluids.c engine/src/nutrition.c engine/src/ecology.c engine/src/fauna.c engine/src/construction.c engine/src/interactions.c engine/src/resources.c engine/src/artifacts.c engine/src/crafting.c engine/src/save.c engine/src/map.c engine/src/chunks.c engine/src/texture_store.c engine/src/glyphs.c engine/src/music_fx.c engine/src/sim.c engine/src/render.c engine/vendor/odm_status.c engine/vendor/odm_rng.c
WASM_EXPORTS := $(shell sed -n 's/^[[:space:]]*\(odg_[A-Za-z0-9_]*\);/-Wl,--export=\1/p' engine/odpar_territorial_domain.exports.map)
TEST_OBJECTS := $(patsubst %.c,$(BUILD)/test-obj/%.o,$(ENGINE))
TEST_DEPS := $(TEST_OBJECTS:.o=.d)
NATIVE_OBJECTS := $(patsubst %.c,$(BUILD)/native-obj/%.o,$(ENGINE))
NATIVE_DEPS := $(NATIVE_OBJECTS:.o=.d)
WASM_OBJECTS := $(patsubst %.c,$(BUILD)/wasm-obj/%.o,$(ENGINE))
WASM_DEPS := $(WASM_OBJECTS:.o=.d)
NATIVE_LIB := $(BUILD)/libodpar_territorial_domain.so
WASM_BIN := $(BUILD)/odpar_territorial_domain.wasm
WASM_WEB := app/web/odpar_territorial_domain.wasm

.PHONY: all native symbols hardening host-check graph graph-check catalog-check content-coupling-check economy-authority-check reserved-contracts-check docs-contract-check build-contract-check quick-gate wasm wasm-smoke test ffi-test ecosystem-test world-systems-test bootstrap-test territory-combat-test construction-test coherence-test bot-economy-test continuity-test soak soak-smoke-test asan bench capture-frame graphics-audit graphics-showcase clean bundle
all: native wasm

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test-obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) -std=c11 -O2 -fno-builtin $(WARN) $(INCLUDES) -MMD -MP -c $< -o $@

-include $(TEST_DEPS) $(NATIVE_DEPS) $(WASM_DEPS)

$(BUILD)/native-obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) -std=c11 -O3 -fPIC -fno-builtin $(NATIVE_HARDEN) $(WARN) $(INCLUDES) -MMD -MP -c $< -o $@

$(NATIVE_LIB): $(NATIVE_OBJECTS) engine/odpar_territorial_domain.exports.map Makefile | $(BUILD)
	$(CC) $(NATIVE_OBJECTS) -shared $(NATIVE_LINK) -o $@

native: $(NATIVE_LIB) symbols hardening

symbols: $(NATIVE_LIB)
	python3 tools/check_native_symbols.py

hardening: $(NATIVE_LIB)
	python3 tools/check_native_hardening.py

host-check:
	python3 tools/check_host_contract.py

graph:
	python3 tools/generate_system_graph.py

graph-check:
	python3 tools/generate_system_graph.py --check

catalog-check:
	python3 tools/check_music_catalog.py

content-coupling-check:
	python3 tools/check_content_coupling.py

economy-authority-check:
	python3 tools/check_economy_authority.py

reserved-contracts-check:
	python3 tools/check_reserved_contracts.py

docs-contract-check:
	python3 tools/check_docs_contract.py

build-contract-check:
	python3 tools/check_build_contract.py

quick-gate: ffi-test ecosystem-test world-systems-test bootstrap-test territory-combat-test construction-test coherence-test bot-economy-test continuity-test soak-smoke-test host-check graph-check catalog-check content-coupling-check economy-authority-check reserved-contracts-check docs-contract-check build-contract-check native wasm-smoke

$(BUILD)/wasm-obj/%.o: %.c
	@mkdir -p $(@D)
	$(CLANG) --target=wasm32 -std=c11 -O3 -ffreestanding -fno-builtin -nostdlib $(INCLUDES) -MMD -MP -c $< -o $@

$(WASM_BIN): $(WASM_OBJECTS) engine/odpar_territorial_domain.exports.map Makefile | $(BUILD)
	$(CLANG) --target=wasm32 -nostdlib $(WASM_OBJECTS) \
		-Wl,--no-entry -Wl,--export-memory -Wl,--initial-memory=16777216 -Wl,--max-memory=33554432 \
		$(WASM_EXPORTS) -o $@

$(WASM_WEB): $(WASM_BIN)
	cp $(WASM_BIN) $(WASM_WEB)

wasm: $(WASM_WEB)

wasm-smoke: $(WASM_WEB)
	node tools/wasm_smoke.mjs

bundle: wasm
	python3 tools/make_bundle.py

$(BUILD)/test_game: tests/test_game.c $(TEST_OBJECTS) | $(BUILD)
	$(CC) -std=c11 -O2 -fno-builtin $(WARN) $(INCLUDES) tests/test_game.c $(TEST_OBJECTS) -o $@

test: $(BUILD)/test_game
	$(BUILD)/test_game
	$(MAKE) ffi-test

$(BUILD)/test_ffi: tests/test_ffi.c $(TEST_OBJECTS) | $(BUILD)
	$(CC) -std=c11 -O2 -fno-builtin $(WARN) $(INCLUDES) tests/test_ffi.c $(TEST_OBJECTS) -o $@

ffi-test: $(BUILD)/test_ffi
	$(BUILD)/test_ffi

$(BUILD)/test_ecosystem: tests/test_ecosystem.c $(TEST_OBJECTS) | $(BUILD)
	$(CC) -std=c11 -O2 -fno-builtin $(WARN) $(INCLUDES) tests/test_ecosystem.c $(TEST_OBJECTS) -o $@

ecosystem-test: $(BUILD)/test_ecosystem
	$(BUILD)/test_ecosystem

$(BUILD)/test_world_systems: tests/test_world_systems.c $(TEST_OBJECTS) | $(BUILD)
	$(CC) -std=c11 -O2 -fno-builtin $(WARN) $(INCLUDES) tests/test_world_systems.c $(TEST_OBJECTS) -o $@

world-systems-test: $(BUILD)/test_world_systems
	$(BUILD)/test_world_systems

$(BUILD)/test_bootstrap: tests/test_bootstrap.c $(TEST_OBJECTS) | $(BUILD)
	$(CC) -std=c11 -O2 -fno-builtin $(WARN) $(INCLUDES) tests/test_bootstrap.c $(TEST_OBJECTS) -o $@

bootstrap-test: $(BUILD)/test_bootstrap
	$(BUILD)/test_bootstrap

$(BUILD)/test_territory_combat: tests/test_territory_combat.c $(TEST_OBJECTS) | $(BUILD)
	$(CC) -std=c11 -O2 -fno-builtin $(WARN) $(INCLUDES) tests/test_territory_combat.c $(TEST_OBJECTS) -o $@

territory-combat-test: $(BUILD)/test_territory_combat
	$(BUILD)/test_territory_combat

$(BUILD)/test_construction: tests/test_construction.c $(TEST_OBJECTS) | $(BUILD)
	$(CC) -std=c11 -O2 -fno-builtin $(WARN) $(INCLUDES) tests/test_construction.c $(TEST_OBJECTS) -o $@

construction-test: $(BUILD)/test_construction
	$(BUILD)/test_construction

$(BUILD)/test_coherence: tests/test_coherence.c $(TEST_OBJECTS) | $(BUILD)
	$(CC) -std=c11 -O2 -fno-builtin $(WARN) $(INCLUDES) tests/test_coherence.c $(TEST_OBJECTS) -o $@

coherence-test: $(BUILD)/test_coherence
	$(BUILD)/test_coherence

$(BUILD)/test_bot_economy: tests/test_bot_economy.c $(TEST_OBJECTS) | $(BUILD)
	$(CC) -std=c11 -O2 -fno-builtin $(WARN) $(INCLUDES) tests/test_bot_economy.c $(TEST_OBJECTS) -o $@

bot-economy-test: $(BUILD)/test_bot_economy
	$(BUILD)/test_bot_economy

$(BUILD)/test_world_continuity: tests/test_world_continuity.c $(TEST_OBJECTS) | $(BUILD)
	$(CC) -std=c11 -O2 -fno-builtin $(WARN) $(INCLUDES) tests/test_world_continuity.c $(TEST_OBJECTS) -o $@

continuity-test: $(BUILD)/test_world_continuity
	$(BUILD)/test_world_continuity

$(BUILD)/soak_game: tests/soak_game.c $(TEST_OBJECTS) | $(BUILD)
	$(CC) -std=c11 -O2 -fno-builtin $(WARN) $(INCLUDES) tests/soak_game.c $(TEST_OBJECTS) -o $@

soak: $(BUILD)/soak_game
	$(BUILD)/soak_game

$(BUILD)/soak_smoke: tests/soak_game.c $(TEST_OBJECTS) | $(BUILD)
	$(CC) -std=c11 -O2 -fno-builtin $(WARN) $(INCLUDES) -DODG_SOAK_TICKS=720u tests/soak_game.c $(TEST_OBJECTS) -o $@

soak-smoke-test: $(BUILD)/soak_smoke
	$(BUILD)/soak_smoke

bench: $(BUILD)
	$(CC) -std=c11 -O3 -fno-builtin $(WARN) $(INCLUDES) $(ENGINE) tools/bench_render.c -o $(BUILD)/bench_render
	$(BUILD)/bench_render

$(BUILD)/capture_frame: tools/capture_frame.c $(TEST_OBJECTS) | $(BUILD)
	$(CC) -std=c11 -O2 -fno-builtin $(WARN) $(INCLUDES) tools/capture_frame.c $(TEST_OBJECTS) -o $@

capture-frame: $(BUILD)/capture_frame

$(BUILD)/capture_graphics_audit: tools/capture_graphics_audit.c $(TEST_OBJECTS) | $(BUILD)
	$(CC) -std=c11 -O2 -fno-builtin $(WARN) $(INCLUDES) tools/capture_graphics_audit.c $(TEST_OBJECTS) -o $@

graphics-audit: $(BUILD)/capture_graphics_audit
	$(BUILD)/capture_graphics_audit

$(BUILD)/capture_graphics_showcase: tools/capture_graphics_showcase.c $(TEST_OBJECTS) | $(BUILD)
	$(CC) -std=c11 -O2 -fno-builtin $(WARN) $(INCLUDES) tools/capture_graphics_showcase.c $(TEST_OBJECTS) -o $@

graphics-showcase: $(BUILD)/capture_graphics_showcase
	$(BUILD)/capture_graphics_showcase

asan: $(BUILD)
	$(CC) -std=c11 -O2 -g -fno-builtin -fsanitize=address,undefined -fno-omit-frame-pointer $(WARN) $(INCLUDES) $(ENGINE) tests/test_game.c -o $(BUILD)/test_game_asan
	ASAN_OPTIONS=$(SANITIZER_OPTIONS) $(BUILD)/test_game_asan
	$(CC) -std=c11 -O2 -g -fno-builtin -fsanitize=address,undefined -fno-omit-frame-pointer $(WARN) $(INCLUDES) $(ENGINE) tests/test_ffi.c -o $(BUILD)/test_ffi_asan
	ASAN_OPTIONS=$(SANITIZER_OPTIONS) $(BUILD)/test_ffi_asan
	$(CC) -std=c11 -O2 -g -fno-builtin -fsanitize=address,undefined -fno-omit-frame-pointer $(WARN) $(INCLUDES) $(ENGINE) tests/soak_game.c -o $(BUILD)/soak_game_asan
	ASAN_OPTIONS=$(SANITIZER_OPTIONS) $(BUILD)/soak_game_asan

clean:
	rm -rf $(BUILD) app/web/odpar_territorial_domain.wasm app/web/ODPAR_Territorial_Domain.html
