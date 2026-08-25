SHELL := /bin/sh
CC ?= clang
PYTHON ?= env PYTHONDONTWRITEBYTECODE=1 python3
BUILD := build
WARN := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Werror
CFLAGS := -std=c11 -O2 -fno-builtin -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fvisibility=hidden $(WARN) -Iengine/include -Iengine/src -pthread
LDFLAGS := -pthread -lm
ENGINE := engine/src/core.c engine/src/world.c engine/src/player.c engine/src/service.c engine/src/raster.c engine/src/android_surface.c engine/src/host_api.c
TESTS := test_camera test_motion test_perf test_raster test_service test_lifetime

.PHONY: all native tests gate generated-check architecture-check product-check legacy-copy-check symbol-check hardening-check clean asan-tests
all: gate

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/libodpar_greenfield.so: $(ENGINE) engine/include/odpar/odg_host.h engine/src/odg_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -fPIC -shared $(ENGINE) -Wl,-z,relro,-z,now,-z,noexecstack -o $@ $(LDFLAGS)

native: $(BUILD)/libodpar_greenfield.so

$(BUILD)/%: tests/%.c $(ENGINE) engine/include/odpar/odg_host.h engine/src/odg_internal.h | $(BUILD)
	$(CC) $(CFLAGS) $< $(ENGINE) -o $@ $(LDFLAGS)

tests: $(addprefix $(BUILD)/,$(TESTS))
	@set -e; for t in $(TESTS); do echo "[TEST] $$t"; ./$(BUILD)/$$t; done

generated-check:
	$(PYTHON) tools/check_generated.py

architecture-check:
	$(PYTHON) tools/check_architecture.py

product-check:
	$(PYTHON) tools/check_product_contract.py

legacy-copy-check:
	$(PYTHON) tools/check_no_legacy_copy.py

symbol-check: native
	$(PYTHON) tools/check_native_symbols.py $(BUILD)/libodpar_greenfield.so

hardening-check: native
	$(PYTHON) tools/check_native_hardening.py $(BUILD)/libodpar_greenfield.so

gate: generated-check architecture-check product-check legacy-copy-check native symbol-check hardening-check tests
	@echo "GREENFIELD FAST GATE: OK"

asan-tests: CFLAGS += -O1 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer
asan-tests: LDFLAGS += -fsanitize=address,undefined
asan-tests: clean tests

clean:
	rm -rf $(BUILD)
