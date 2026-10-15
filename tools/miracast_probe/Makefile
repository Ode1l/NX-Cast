.DEFAULT_GOAL := all

ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is required; use scripts/build_miracast_probe.sh)
endif

include $(DEVKITPRO)/libnx/switch_rules

PROBE_DIR := ../../build/miracast-probe
PROBE_OUTPUT := $(PROBE_DIR)/NX-Cast-Miracast-Probe
PROBE_REVISION := $(shell git rev-parse --short=12 HEAD)-worktree
PROBE_ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

# One explicit, independent build. Never touches the player's NRO or libraries.
.PHONY: all
all:
	mkdir -p "$(PROBE_DIR)"
	$(CC) -std=gnu11 -O2 -g -Wall -Wextra -Werror $(PROBE_ARCH) \
		-D__SWITCH__ -DPROBE_REVISION='"$(PROBE_REVISION)"' \
		-I"$(LIBNX)/include" -c main.c -o "$(PROBE_OUTPUT).o"
	$(CC) $(PROBE_ARCH) -specs="$(LIBNX)/switch.specs" \
		"$(PROBE_OUTPUT).o" -L"$(LIBNX)/lib" -lnx \
		-Wl,-Map,"$(PROBE_OUTPUT).map" -o "$(PROBE_OUTPUT).elf"
	nacptool --create "NX-Cast Miracast Probe" "NX-Cast" "M0" "$(PROBE_OUTPUT).nacp"
	elf2nro "$(PROBE_OUTPUT).elf" "$(PROBE_OUTPUT).nro" --nacp="$(PROBE_OUTPUT).nacp"
