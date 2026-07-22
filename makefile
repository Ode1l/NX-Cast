#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
THIS_MAKEFILE ?= $(TOPDIR)/makefile
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# NO_ICON: if set to anything, do not use icon.
# NO_NACP: if set to anything, no .nacp file is generated.
# APP_TITLE is the name of the app stored in the .nacp file (Optional)
# APP_AUTHOR is the author of the app stored in the .nacp file (Optional)
# APP_VERSION is the version of the app stored in the .nacp file (Optional)
# APP_TITLEID is the titleID of the app stored in the .nacp file (Optional)
# ICON is the filename of the icon (.jpg), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.jpg
#     - icon.jpg
#     - <libnx folder>/default_icon.jpg
#
# CONFIG_JSON is the filename of the NPDM config file (.json), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.json
#     - config.json
#   If a JSON file is provided or autodetected, an ExeFS PFS0 (.nsp) is built instead
#   of a homebrew executable (.nro). This is intended to be used for sysmodules.
#   NACP building is skipped as well.
#---------------------------------------------------------------------------------
TARGET		:=	NX-Cast
APP_TITLE	:=	NX-Cast
APP_AUTHOR	:=	Ode1l
APP_VERSION	:=	0.2.0
ICON		:=	assets/icon/switch-screencast-logo.jpg
BUILD		:=	build
SOURCES		:=	source \
			source/app \
			source/iptv \
			source/log \
			source/player \
			source/player/ui \
			source/player/core \
			source/player/backend \
			source/player/render \
			source/protocol \
			source/protocol/dlna \
			source/protocol/dlna/control \
			source/protocol/dlna/control/action \
			source/protocol/http \
			source/protocol/dlna/discovery \
			source/protocol/dlna/description \
			source/protocol/airplay \
			source/protocol/airplay/protocol \
			source/protocol/airplay/security \
			source/protocol/airplay/discovery \
			source/protocol/airplay/mirror \
			source/protocol/airplay/media \
			third_party/playfair
DATA		:=	data
INCLUDES	:=	include source third_party/playfair

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS	:=	-g -Wall -O2 -ffunction-sections \
			$(ARCH) $(DEFINES)

CFLAGS	+=	$(INCLUDE) -D__SWITCH__ -DNXCAST_APP_VERSION=\"$(APP_VERSION)\"

TRACE_MEDIA ?= 0
TRACE_INPUT ?= 0
TRACE_AIRPLAY ?= 0
NXCAST_DIAG_PROFILE ?= normal
NXCAST_AIRPLAY_RUNTIME ?= 1
NXCAST_REQUIRE_LIBMPV ?= 0
NXCAST_REQUIRE_DEKO3D ?= 0
NXCAST_REQUIRE_AIRPLAY_ED25519 ?= 0
NXCAST_USE_IMGUI_UI ?= 0
RELEASE_JOBS ?= 4
RELEASE_ATTESTATION := $(CURDIR)/$(BUILD)/release-features.txt
HOST_CC ?= cc
HOST_CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -Isource -Ithird_party/playfair
HOST_SANITIZER_FLAGS ?= -fsanitize=address,undefined -fno-omit-frame-pointer
HOST_THREAD_FLAGS ?= -pthread
NETWORK_DIAGNOSTICS_SOURCE := source/app/network_diagnostics.c
RUNTIME_DIAGNOSTICS_SOURCE := source/app/runtime_diagnostics.c
AIRPLAY_OBSERVABILITY_TEST_FLAGS := -DNXCAST_RUNTIME_OBSERVABILITY=1 -DNXCAST_AIRPLAY_TRACE_VERBOSE=1
PLAYFAIR_SOURCES := third_party/playfair/hand_garble.c \
	third_party/playfair/modified_md5.c \
	third_party/playfair/omg_hax.c \
	third_party/playfair/playfair_decrypt.c \
	third_party/playfair/replies.c \
	third_party/playfair/sap_hash.c
PLAYFAIR_LIBS := -lm
AIRPLAY_LIFECYCLE_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_airplay
AIRPLAY_PLIST_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_airplay_plist
AIRPLAY_RTSP_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_airplay_rtsp
AIRPLAY_RTSP_STACK_CHECK_OBJ := $(CURDIR)/$(BUILD)/tests/airplay_rtsp_stack_check.o
AIRPLAY_CRYPTO_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_airplay_crypto
AIRPLAY_SRP_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_airplay_srp
AIRPLAY_PAIRING_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_airplay_pairing
AIRPLAY_DNS_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_airplay_dns
AIRPLAY_MDNS_SUSPEND_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_airplay_mdns_suspend
AIRPLAY_SERVER_LIFECYCLE_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_airplay_server_lifecycle
SEEK_TARGET_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_seek_target
AIRPLAY_HANDLERS_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_airplay_handlers
AIRPLAY_FAIRPLAY_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_airplay_fairplay
AIRPLAY_MIRROR_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_airplay_mirror
AIRPLAY_TIMING_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_airplay_timing
AIRPLAY_STREAM_BRIDGE_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_airplay_stream_bridge
AIRPLAY_MIRROR_RUNTIME_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_airplay_mirror_runtime
AIRPLAY_AUDIO_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_airplay_audio
AIRPLAY_CLOCK_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_airplay_clock
AIRPLAY_REMOTE_VIDEO_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_airplay_remote_video
PLAYER_OWNERSHIP_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_player_ownership
PLAYER_ACTOR_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_media_actor
PROTOCOL_COORDINATOR_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_protocol_coordinator
DLNA_CONTROLLER_SESSION_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_dlna_controller_session
NETWORK_DIAGNOSTICS_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_network_diagnostics
RUNTIME_DIAGNOSTICS_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_runtime_diagnostics
LOG_MIRROR_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_log_mirror
LOG_POLICY_NORMAL_OBJ := $(CURDIR)/$(BUILD)/tests/test_log_policy_normal.o
LOG_POLICY_TRACE_OBJ := $(CURDIR)/$(BUILD)/tests/test_log_policy_trace.o
C_SIZE_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_c_size
SOAP_WRITER_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_soap_writer
PLAYER_TYPES_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_player_types
IPTV_URL_TEST_BIN := $(CURDIR)/$(BUILD)/tests/test_iptv_url
AIRPLAY_SMOKE_SERVER_BIN := $(CURDIR)/$(BUILD)/tests/airplay_smoke_server
AIRPLAY_PAIRING_SMOKE_SERVER_BIN := $(CURDIR)/$(BUILD)/tests/airplay_pairing_smoke_server
AIRPLAY_MDNS_SMOKE_SERVER_BIN := $(CURDIR)/$(BUILD)/tests/airplay_mdns_smoke_server
AIRPLAY_RECEIVER_SMOKE_SERVER_BIN := $(CURDIR)/$(BUILD)/tests/airplay_receiver_smoke_server

NXCAST_DIAG_PROFILES := normal airplay-off control-only mdns-socket mdns-idle \
	mdns-receive full-parallel full-serial full-low-priority mdns-receive-bsd8 \
	mdns-receive-bsd16 full-discovery-suspend-bsd8 \
	full-mdns-playback-suspend-bsd8 full-owner-exclusive-bsd12 \
	full-owner-exclusive-observe-bsd12

ifeq ($(filter $(NXCAST_DIAG_PROFILE),$(NXCAST_DIAG_PROFILES)),)
$(error Unknown NXCAST_DIAG_PROFILE '$(NXCAST_DIAG_PROFILE)'; expected one of: $(NXCAST_DIAG_PROFILES))
endif

ifneq ($(NXCAST_DIAG_PROFILE),normal)
CFLAGS += -DNXCAST_DIAGNOSTIC_BUILD=1 \
	-DNXCAST_MEDIA_TRACE_VERBOSE=1 \
	-DNXCAST_INPUT_TRACE_VERBOSE=1 \
	-DNXCAST_AIRPLAY_TRACE_VERBOSE=1 \
	-DNXCAST_TRACE_BUILD=1
endif

ifeq ($(NXCAST_DIAG_PROFILE),airplay-off)
CFLAGS += -DNXCAST_DIAG_PROFILE_ID=1 \
	-DNXCAST_DIAG_PROFILE_NAME=\"airplay-off\" \
	-DNXCAST_DISABLE_AIRPLAY_RUNTIME=1
else ifeq ($(NXCAST_DIAG_PROFILE),control-only)
CFLAGS += -DNXCAST_DIAG_PROFILE_ID=2 \
	-DNXCAST_DIAG_PROFILE_NAME=\"control-only\" \
	-DNXCAST_AIRPLAY_DISCOVERY_ENABLED=0
else ifeq ($(NXCAST_DIAG_PROFILE),mdns-socket)
CFLAGS += -DNXCAST_DIAG_PROFILE_ID=3 \
	-DNXCAST_DIAG_PROFILE_NAME=\"mdns-socket\" \
	-DNXCAST_AIRPLAY_MDNS_DIAG_MODE=1
else ifeq ($(NXCAST_DIAG_PROFILE),mdns-idle)
CFLAGS += -DNXCAST_DIAG_PROFILE_ID=4 \
	-DNXCAST_DIAG_PROFILE_NAME=\"mdns-idle\" \
	-DNXCAST_AIRPLAY_MDNS_DIAG_MODE=2
else ifeq ($(NXCAST_DIAG_PROFILE),mdns-receive)
CFLAGS += -DNXCAST_DIAG_PROFILE_ID=5 \
	-DNXCAST_DIAG_PROFILE_NAME=\"mdns-receive\" \
	-DNXCAST_AIRPLAY_MDNS_DIAG_MODE=3
else ifeq ($(NXCAST_DIAG_PROFILE),full-parallel)
CFLAGS += -DNXCAST_DIAG_PROFILE_ID=6 \
	-DNXCAST_DIAG_PROFILE_NAME=\"full-parallel\"
else ifeq ($(NXCAST_DIAG_PROFILE),full-serial)
CFLAGS += -DNXCAST_DIAG_PROFILE_ID=7 \
	-DNXCAST_DIAG_PROFILE_NAME=\"full-serial\" \
	-DNXCAST_PROTOCOL_START_SERIAL=1
else ifeq ($(NXCAST_DIAG_PROFILE),full-low-priority)
CFLAGS += -DNXCAST_DIAG_PROFILE_ID=8 \
	-DNXCAST_DIAG_PROFILE_NAME=\"full-low-priority\" \
	-DNXCAST_AIRPLAY_MDNS_THREAD_PRIORITY=0x2e
else ifeq ($(NXCAST_DIAG_PROFILE),mdns-receive-bsd8)
CFLAGS += -DNXCAST_DIAG_PROFILE_ID=9 \
	-DNXCAST_DIAG_PROFILE_NAME=\"mdns-receive-bsd8\" \
	-DNXCAST_AIRPLAY_MDNS_DIAG_MODE=3 \
	-DNXCAST_SOCKET_BSD_SESSIONS=8
else ifeq ($(NXCAST_DIAG_PROFILE),mdns-receive-bsd16)
CFLAGS += -DNXCAST_DIAG_PROFILE_ID=10 \
	-DNXCAST_DIAG_PROFILE_NAME=\"mdns-receive-bsd16\" \
	-DNXCAST_AIRPLAY_MDNS_DIAG_MODE=3 \
	-DNXCAST_SOCKET_BSD_SESSIONS=16
else ifeq ($(NXCAST_DIAG_PROFILE),full-discovery-suspend-bsd8)
CFLAGS += -DNXCAST_DIAG_PROFILE_ID=11 \
	-DNXCAST_DIAG_PROFILE_NAME=\"full-discovery-suspend-bsd8\" \
	-DNXCAST_SOCKET_BSD_SESSIONS=8 \
	-DNXCAST_SUSPEND_DISCOVERY_WHILE_MEDIA=1
else ifeq ($(NXCAST_DIAG_PROFILE),full-mdns-playback-suspend-bsd8)
CFLAGS += -DNXCAST_DIAG_PROFILE_ID=12 \
	-DNXCAST_DIAG_PROFILE_NAME=\"full-mdns-playback-suspend-bsd8\" \
	-DNXCAST_SOCKET_BSD_SESSIONS=8 \
	-DNXCAST_SUSPEND_AIRPLAY_MDNS_WHILE_PLAYBACK=1
else ifeq ($(NXCAST_DIAG_PROFILE),full-owner-exclusive-bsd12)
CFLAGS += -DNXCAST_DIAG_PROFILE_ID=13 \
	-DNXCAST_DIAG_PROFILE_NAME=\"full-owner-exclusive-bsd12\" \
	-DNXCAST_SOCKET_BSD_SESSIONS=12 \
	-DNXCAST_SOCKET_SB_EFFICIENCY=8 \
	-DNXCAST_DLNA_CONTROLLER_EXIT_TIMEOUT_MS=10000 \
	-DNXCAST_EXCLUSIVE_MEDIA_RESOURCES=1
else ifeq ($(NXCAST_DIAG_PROFILE),full-owner-exclusive-observe-bsd12)
CFLAGS += -DNXCAST_DIAG_PROFILE_ID=14 \
	-DNXCAST_DIAG_PROFILE_NAME=\"full-owner-exclusive-observe-bsd12\" \
	-DNXCAST_SOCKET_BSD_SESSIONS=12 \
	-DNXCAST_SOCKET_SB_EFFICIENCY=8 \
	-DNXCAST_DLNA_CONTROLLER_EXIT_TIMEOUT_MS=10000 \
	-DNXCAST_EXCLUSIVE_MEDIA_RESOURCES=1 \
	-DNXCAST_RUNTIME_OBSERVABILITY=1
else
CFLAGS += -DNXCAST_DIAG_PROFILE_ID=0 \
	-DNXCAST_DIAG_PROFILE_NAME=\"normal\"
endif

ifeq ($(TRACE_MEDIA),1)
CFLAGS	+=	-DNXCAST_MEDIA_TRACE_VERBOSE=1
endif

ifeq ($(TRACE_INPUT),1)
CFLAGS	+=	-DNXCAST_INPUT_TRACE_VERBOSE=1
endif

ifeq ($(TRACE_AIRPLAY),1)
CFLAGS	+=	-DNXCAST_AIRPLAY_TRACE_VERBOSE=1
endif

ifneq ($(filter 1,$(TRACE_MEDIA) $(TRACE_INPUT) $(TRACE_AIRPLAY)),)
CFLAGS	+=	-DNXCAST_TRACE_BUILD=1
endif

ifeq ($(NXCAST_AIRPLAY_RUNTIME),0)
CFLAGS	+=	-DNXCAST_DISABLE_AIRPLAY_RUNTIME=1
endif

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lnx -lm

PKG_CONFIG	?= pkg-config
MPV_PKG_CONFIG_PATH := $(PORTLIBS_PREFIX)/lib/pkgconfig
EGL_GLES_PKG_CONFIG_PATH := $(PORTLIBS_PREFIX)/lib/pkgconfig
MBEDTLS_PKG_CONFIG_PATH := $(PORTLIBS_PREFIX)/lib/pkgconfig
HOST_MBEDTLS_PREFIX ?= $(shell brew --prefix mbedtls@2 2>/dev/null)
HOST_MBEDTLS_PKG_CONFIG_PATH ?= $(HOST_MBEDTLS_PREFIX)/lib/pkgconfig
HOST_MBEDTLS_FOUND := $(shell PKG_CONFIG_PATH="$(HOST_MBEDTLS_PKG_CONFIG_PATH):$${PKG_CONFIG_PATH}" $(PKG_CONFIG) --exists mbedcrypto >/dev/null 2>&1 && echo 1)
HOST_MBEDTLS_CFLAGS := $(shell PKG_CONFIG_PATH="$(HOST_MBEDTLS_PKG_CONFIG_PATH):$${PKG_CONFIG_PATH}" $(PKG_CONFIG) --cflags mbedcrypto 2>/dev/null)
HOST_MBEDTLS_LIBS := $(shell PKG_CONFIG_PATH="$(HOST_MBEDTLS_PKG_CONFIG_PATH):$${PKG_CONFIG_PATH}" $(PKG_CONFIG) --libs mbedcrypto 2>/dev/null)
HOST_MBEDTLS_SYSTEM_FOUND := $(shell test -f /usr/include/mbedtls/version.h && test "$$($(HOST_CC) -print-file-name=libmbedcrypto.a)" != "libmbedcrypto.a" && echo 1)
ifeq ($(HOST_MBEDTLS_FOUND),)
ifeq ($(HOST_MBEDTLS_SYSTEM_FOUND),1)
HOST_MBEDTLS_FOUND := 1
HOST_MBEDTLS_LIBS := -lmbedcrypto
endif
endif
MBEDTLS_FOUND := $(shell PKG_CONFIG_PATH="$(MBEDTLS_PKG_CONFIG_PATH)" $(PKG_CONFIG) --exists mbedcrypto >/dev/null 2>&1 && echo 1)
MBEDTLS_CFLAGS := $(shell PKG_CONFIG_PATH="$(MBEDTLS_PKG_CONFIG_PATH)" $(PKG_CONFIG) --cflags mbedcrypto 2>/dev/null)
MBEDTLS_LIBS := $(shell PKG_CONFIG_PATH="$(MBEDTLS_PKG_CONFIG_PATH)" $(PKG_CONFIG) --libs mbedcrypto 2>/dev/null)
HOST_SODIUM_PREFIX ?= $(shell brew --prefix libsodium 2>/dev/null)
HOST_SODIUM_PKG_CONFIG_PATH ?= $(HOST_SODIUM_PREFIX)/lib/pkgconfig
HOST_SODIUM_FOUND := $(shell PKG_CONFIG_PATH="$(HOST_SODIUM_PKG_CONFIG_PATH):$${PKG_CONFIG_PATH}" $(PKG_CONFIG) --exists libsodium >/dev/null 2>&1 && echo 1)
HOST_SODIUM_CFLAGS := $(shell PKG_CONFIG_PATH="$(HOST_SODIUM_PKG_CONFIG_PATH):$${PKG_CONFIG_PATH}" $(PKG_CONFIG) --cflags libsodium 2>/dev/null)
HOST_SODIUM_LIBS := $(shell PKG_CONFIG_PATH="$(HOST_SODIUM_PKG_CONFIG_PATH):$${PKG_CONFIG_PATH}" $(PKG_CONFIG) --libs libsodium 2>/dev/null)
HOST_FFMPEG_PREFIX ?= $(shell brew --prefix ffmpeg 2>/dev/null)
HOST_FFMPEG_PKG_CONFIG_PATH ?= $(HOST_FFMPEG_PREFIX)/lib/pkgconfig
HOST_FFMPEG_FOUND := $(shell PKG_CONFIG_PATH="$(HOST_FFMPEG_PKG_CONFIG_PATH):$${PKG_CONFIG_PATH}" $(PKG_CONFIG) --exists libavformat libavcodec libavutil >/dev/null 2>&1 && echo 1)
HOST_FFMPEG_CFLAGS := $(shell PKG_CONFIG_PATH="$(HOST_FFMPEG_PKG_CONFIG_PATH):$${PKG_CONFIG_PATH}" $(PKG_CONFIG) --cflags libavformat libavcodec libavutil 2>/dev/null)
HOST_FFMPEG_LIBS := $(shell PKG_CONFIG_PATH="$(HOST_FFMPEG_PKG_CONFIG_PATH):$${PKG_CONFIG_PATH}" $(PKG_CONFIG) --libs libavformat libavcodec libavutil 2>/dev/null)
SODIUM_FOUND := $(shell PKG_CONFIG_PATH="$(PORTLIBS_PREFIX)/lib/pkgconfig" $(PKG_CONFIG) --exists libsodium >/dev/null 2>&1 && echo 1)
SODIUM_CFLAGS := $(shell PKG_CONFIG_PATH="$(PORTLIBS_PREFIX)/lib/pkgconfig" $(PKG_CONFIG) --cflags libsodium 2>/dev/null)
SODIUM_LIBS := $(shell PKG_CONFIG_PATH="$(PORTLIBS_PREFIX)/lib/pkgconfig" $(PKG_CONFIG) --libs libsodium 2>/dev/null)
MPV_FOUND := $(shell PKG_CONFIG_PATH="$(MPV_PKG_CONFIG_PATH)" $(PKG_CONFIG) --exists mpv >/dev/null 2>&1 && echo 1)
MPV_STATIC_LIBS := $(shell PKG_CONFIG_PATH="$(MPV_PKG_CONFIG_PATH)" $(PKG_CONFIG) --static --libs mpv 2>/dev/null)
MPV_RENDER_GL_HEADER_FOUND := $(shell test -f "$(PORTLIBS_PREFIX)/include/mpv/render_gl.h" && echo 1)
MPV_RENDER_DK3D_HEADER_FOUND := $(shell test -f "$(PORTLIBS_PREFIX)/include/mpv/render_dk3d.h" && echo 1)
FFMPEG_NVTEGRA_HEADER_FOUND := $(shell test -f "$(PORTLIBS_PREFIX)/include/libavutil/hwcontext_nvtegra.h" && echo 1)
SWITCH_EGL_GLES_FOUND := $(shell test -f "$(PORTLIBS_PREFIX)/include/EGL/egl.h" && test -f "$(PORTLIBS_PREFIX)/include/GLES2/gl2.h" && echo 1)
MPV_EXPLICIT_NVTEGRA_HWDEC_FOUND := $(shell strings "$(PORTLIBS_PREFIX)/lib/libmpv.a" 2>/dev/null | grep -q nvtegra && echo 1)
MPV_USES_UAM := $(shell printf '%s\n' "$(MPV_STATIC_LIBS)" | grep -q -- ' -luam' && echo 1)
MPV_USES_DEKO3D := $(shell printf '%s\n' "$(MPV_STATIC_LIBS)" | grep -q -- ' -ldeko3d' && echo 1)
EGL_GLES_LIBS := $(shell PKG_CONFIG_PATH="$(EGL_GLES_PKG_CONFIG_PATH)" $(PKG_CONFIG) --static --libs egl glesv2)

ifeq ($(MBEDTLS_FOUND),)
$(error mbedTLS mbedcrypto was not found in $(MBEDTLS_PKG_CONFIG_PATH). Install the devkitPro switch-mbedtls package)
endif

CFLAGS += $(MBEDTLS_CFLAGS)
LIBS += $(MBEDTLS_LIBS)

ifeq ($(SODIUM_FOUND),1)
CFLAGS += $(SODIUM_CFLAGS) -DAIRPLAY_CRYPTO_HAVE_ED25519=1
LIBS += $(SODIUM_LIBS)
endif

ifeq ($(NXCAST_REQUIRE_AIRPLAY_ED25519),1)
ifeq ($(SODIUM_FOUND),)
$(error NXCAST_REQUIRE_AIRPLAY_ED25519=1 but switch-libsodium was not found. Install it with dkp-pacman)
endif
endif

ifeq ($(MPV_FOUND),1)
CFLAGS	+=	-DHAVE_LIBMPV
LIBS	+= $(MPV_STATIC_LIBS)
endif

ifeq ($(MPV_USES_UAM)$(MPV_USES_DEKO3D),11)
CFLAGS	+=	-DHAVE_MPV_RENDER_DK3D
else ifeq ($(MPV_RENDER_DK3D_HEADER_FOUND),1)
CFLAGS	+=	-DHAVE_MPV_RENDER_DK3D
endif

ifeq ($(MPV_RENDER_DK3D_HEADER_FOUND),1)
ifeq ($(MPV_USES_UAM)$(MPV_USES_DEKO3D),11)
DEKO3D_RENDER_ACTIVE := 1
endif
endif

ifeq ($(DEKO3D_RENDER_ACTIVE),)
ifeq ($(MPV_RENDER_GL_HEADER_FOUND),1)
CFLAGS	+=	-DHAVE_MPV_RENDER_GL
endif
endif

ifeq ($(MPV_EXPLICIT_NVTEGRA_HWDEC_FOUND),1)
CFLAGS	+=	-DHAVE_MPV_EXPLICIT_NVTEGRA_HWDEC
endif

ifeq ($(FFMPEG_NVTEGRA_HEADER_FOUND),1)
CFLAGS	+=	-DHAVE_NVTEGRA_HWCONTEXT
endif

ifeq ($(NXCAST_REQUIRE_LIBMPV),1)
ifeq ($(MPV_FOUND),)
$(error NXCAST_REQUIRE_LIBMPV=1 but mpv was not found via pkg-config. Check PORTLIBS_PREFIX and switch-libmpv installation)
endif
endif

ifeq ($(NXCAST_REQUIRE_DEKO3D),1)
ifeq ($(DEKO3D_RENDER_ACTIVE),)
$(error NXCAST_REQUIRE_DEKO3D=1 but deko3d libmpv render support is not active. Check switch-libmpv_deko3d and libuam packages)
endif
endif

ifeq ($(DEKO3D_RENDER_ACTIVE),)
ifeq ($(SWITCH_EGL_GLES_FOUND),1)
CFLAGS	+=	-DHAVE_SWITCH_EGL_GLES
LIBS	+=	$(EGL_GLES_LIBS)
endif
endif

ifeq ($(NXCAST_USE_IMGUI_UI),1)
ifeq ($(DEKO3D_RENDER_ACTIVE),)
$(error NXCAST_USE_IMGUI_UI=1 requires active deko3d libmpv render support)
endif
SOURCES += source/player/render/imgui third_party/imgui
INCLUDES += third_party/imgui
CFLAGS += -DNXCAST_USE_IMGUI_UI=1
endif

CXXFLAGS	:= $(CFLAGS) -std=gnu++17 -fno-rtti -fno-exceptions

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(LIBNX)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifeq ($(NXCAST_IN_BUILD),)
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(TOPDIR)/$(TARGET)
export TOPDIR

export VPATH	:=	$(foreach dir,$(SOURCES),$(TOPDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(TOPDIR)/$(dir))

export DEPSDIR	:=	$(TOPDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
ifeq ($(DEKO3D_RENDER_ACTIVE),1)
	export LD	:=	$(CXX)
else
	export LD	:=	$(CC)
endif
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(TOPDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(TOPDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(CONFIG_JSON)),)
	jsons := $(wildcard *.json)
	ifneq (,$(findstring $(TARGET).json,$(jsons)))
		export APP_JSON := $(TOPDIR)/$(TARGET).json
	else
		ifneq (,$(findstring config.json,$(jsons)))
			export APP_JSON := $(TOPDIR)/config.json
		endif
	endif
else
	export APP_JSON := $(TOPDIR)/$(CONFIG_JSON)
endif

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
	export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
	export NROFLAGS += --nacp=$(TOPDIR)/$(TARGET).nacp
endif

ifneq ($(APP_TITLEID),)
	export NACPFLAGS += --titleid=$(APP_TITLEID)
endif

.PHONY: $(BUILD) clean all release-build test-airplay test-airplay-dns test-airplay-mdns-suspend test-airplay-server-lifecycle test-c-safety test-c-safety-sanitize test-c-size test-soap-writer test-player-types test-seek-target test-iptv-url test-log-mirror test-log-policy test-network-diagnostics test-runtime-diagnostics test-player-actor test-protocol-coordinator test-dlna-controller-session test-shutdown-order

#---------------------------------------------------------------------------------
all: sdmc_init $(BUILD)

sdmc_init:
	@echo "Preparing SDMC directory structure..."
	@mkdir -p $(TOPDIR)/sdmc/switch/NX-Cast/dlna
	@if [ -d "$(TOPDIR)/assets/dlna" ]; then cp -v $(TOPDIR)/assets/dlna/* $(TOPDIR)/sdmc/switch/NX-Cast/dlna/; else echo "Warning: assets/dlna not found"; fi
	@mkdir -p $(TOPDIR)/sdmc/switch/NX-Cast/fonts
	@if [ -d "$(TOPDIR)/assets/fonts" ]; then cp -v $(TOPDIR)/assets/fonts/* $(TOPDIR)/sdmc/switch/NX-Cast/fonts/; else echo "Warning: assets/fonts not found"; fi
	@mkdir -p $(TOPDIR)/sdmc/switch/NX-Cast/iptv
	@if [ -d "$(TOPDIR)/assets/iptv" ]; then cp -v $(TOPDIR)/assets/iptv/* $(TOPDIR)/sdmc/switch/NX-Cast/iptv/; else echo "Warning: assets/iptv not found"; fi
	@mkdir -p $(TOPDIR)/sdmc/switch/NX-Cast/airplay
	@if [ -d "$(TOPDIR)/assets/airplay" ]; then cp -v $(TOPDIR)/assets/airplay/* $(TOPDIR)/sdmc/switch/NX-Cast/airplay/; else echo "Warning: assets/airplay not found"; fi
	@mkdir -p $(TOPDIR)/sdmc/switch/NX-Cast/licenses
	@if [ -d "$(TOPDIR)/assets/licenses" ]; then cp -v $(TOPDIR)/assets/licenses/* $(TOPDIR)/sdmc/switch/NX-Cast/licenses/; else echo "Warning: assets/licenses not found"; fi
	@ls -la $(TOPDIR)/sdmc/switch/NX-Cast/dlna/ 2>/dev/null || echo "SDMC dlna directory created (contents will be populated at runtime)"

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(THIS_MAKEFILE) NXCAST_IN_BUILD=1

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
ifeq ($(strip $(APP_JSON)),)
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf
else
	@rm -fr $(BUILD) $(TARGET).nsp $(TARGET).nso $(TARGET).npdm $(TARGET).elf
endif

release-build:
	@$(MAKE) clean
	@$(MAKE) NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 -j$(RELEASE_JOBS)
	@strings $(TOPDIR)/$(TARGET).nro | grep -Fxq 'libnx-kernel-chacha' || (printf '%s\n' 'Release NRO does not contain the libnx-backed libsodium random source.' >&2; exit 1)
	@printf '%s\n' \
		'nxcast-release-v1' \
		'libmpv=1' \
		'deko3d=1' \
		'airplay-ed25519=1' \
		'airplay-randombytes=libnx' \
		'airplay-playfair=1' > $(RELEASE_ATTESTATION)
	@echo "release build attested at $(RELEASE_ATTESTATION)"

test-protocol-coordinator:
	@mkdir -p $(dir $(PROTOCOL_COORDINATOR_TEST_BIN))
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_THREAD_FLAGS) source/player/core/ownership.c source/app/protocol_coordinator.c scripts/test_protocol_coordinator.c -o $(PROTOCOL_COORDINATOR_TEST_BIN)
	@$(PROTOCOL_COORDINATOR_TEST_BIN)

test-dlna-controller-session:
	@mkdir -p "$(CURDIR)/$(BUILD)/tests"
	$(HOST_CC) $(HOST_CFLAGS) source/protocol/dlna/control/controller_session.c scripts/test_dlna_controller_session.c -o "$(DLNA_CONTROLLER_SESSION_TEST_BIN)"
	@"$(DLNA_CONTROLLER_SESSION_TEST_BIN)"

test-network-diagnostics:
	@mkdir -p $(dir $(NETWORK_DIAGNOSTICS_TEST_BIN))
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_THREAD_FLAGS) source/app/network_diagnostics.c scripts/test_network_diagnostics.c -o $(NETWORK_DIAGNOSTICS_TEST_BIN)
	@$(NETWORK_DIAGNOSTICS_TEST_BIN)

test-runtime-diagnostics:
	@mkdir -p $(dir $(RUNTIME_DIAGNOSTICS_TEST_BIN))
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_THREAD_FLAGS) source/app/network_diagnostics.c source/app/runtime_diagnostics.c scripts/test_runtime_diagnostics.c -o $(RUNTIME_DIAGNOSTICS_TEST_BIN)
	@$(RUNTIME_DIAGNOSTICS_TEST_BIN)

test-player-actor:
	@mkdir -p $(dir $(PLAYER_ACTOR_TEST_BIN))
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_THREAD_FLAGS) source/player/core/media_actor.c scripts/test_media_actor.c -o $(PLAYER_ACTOR_TEST_BIN)
	@$(PLAYER_ACTOR_TEST_BIN)

test-log-mirror:
	@mkdir -p $(dir $(LOG_MIRROR_TEST_BIN))
	$(HOST_CC) $(HOST_CFLAGS) source/log/mirror.c scripts/test_log_mirror.c -o $(LOG_MIRROR_TEST_BIN)
	@$(LOG_MIRROR_TEST_BIN)

test-log-policy:
	@mkdir -p $(dir $(LOG_POLICY_NORMAL_OBJ))
	$(HOST_CC) $(HOST_CFLAGS) -DNXCAST_EXPECTED_LOG_LEVEL=LOG_LEVEL_WARN -c scripts/test_log_policy.c -o $(LOG_POLICY_NORMAL_OBJ)
	$(HOST_CC) $(HOST_CFLAGS) -DNXCAST_TRACE_BUILD=1 -DNXCAST_EXPECTED_LOG_LEVEL=LOG_LEVEL_INFO -c scripts/test_log_policy.c -o $(LOG_POLICY_TRACE_OBJ)

test-c-size:
	@mkdir -p $(dir $(C_SIZE_TEST_BIN))
	$(HOST_CC) $(HOST_CFLAGS) scripts/test_c_size.c -o $(C_SIZE_TEST_BIN)
	@$(C_SIZE_TEST_BIN)

test-soap-writer:
	@mkdir -p $(dir $(SOAP_WRITER_TEST_BIN))
	$(HOST_CC) $(HOST_CFLAGS) source/protocol/dlna/control/soap_writer.c scripts/test_soap_writer.c -o $(SOAP_WRITER_TEST_BIN)
	@$(SOAP_WRITER_TEST_BIN)

test-player-types:
	@mkdir -p $(dir $(PLAYER_TYPES_TEST_BIN))
	$(HOST_CC) $(HOST_CFLAGS) source/player/types.c scripts/test_player_types.c -o $(PLAYER_TYPES_TEST_BIN)
	@$(PLAYER_TYPES_TEST_BIN)

test-seek-target:
	@mkdir -p $(dir $(SEEK_TARGET_TEST_BIN))
	$(HOST_CC) $(HOST_CFLAGS) source/player/seek_target.c scripts/test_seek_target.c -lm -o $(SEEK_TARGET_TEST_BIN)
	@$(SEEK_TARGET_TEST_BIN)

test-iptv-url:
	@mkdir -p $(dir $(IPTV_URL_TEST_BIN))
	$(HOST_CC) $(HOST_CFLAGS) source/iptv/url.c scripts/test_iptv_url.c -o $(IPTV_URL_TEST_BIN)
	@$(IPTV_URL_TEST_BIN)

test-airplay-dns:
	@mkdir -p $(dir $(AIRPLAY_DNS_TEST_BIN))
	$(HOST_CC) $(HOST_CFLAGS) source/protocol/airplay/discovery/dns.c scripts/test_airplay_dns.c -o $(AIRPLAY_DNS_TEST_BIN)
	@$(AIRPLAY_DNS_TEST_BIN)

test-airplay-mdns-suspend:
	@mkdir -p $(dir $(AIRPLAY_MDNS_SUSPEND_TEST_BIN))
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_THREAD_FLAGS) $(AIRPLAY_OBSERVABILITY_TEST_FLAGS) -DAIRPLAY_TESTING=1 $(NETWORK_DIAGNOSTICS_SOURCE) $(RUNTIME_DIAGNOSTICS_SOURCE) source/protocol/airplay/discovery/dns.c source/protocol/airplay/discovery/mdns.c scripts/test_airplay_mdns_suspend.c -o $(AIRPLAY_MDNS_SUSPEND_TEST_BIN)
	@$(AIRPLAY_MDNS_SUSPEND_TEST_BIN)

test-airplay-server-lifecycle:
	@mkdir -p $(dir $(AIRPLAY_SERVER_LIFECYCLE_TEST_BIN))
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_THREAD_FLAGS) $(AIRPLAY_OBSERVABILITY_TEST_FLAGS) $(NETWORK_DIAGNOSTICS_SOURCE) $(RUNTIME_DIAGNOSTICS_SOURCE) source/protocol/airplay/protocol/rtsp.c source/protocol/airplay/server.c scripts/test_airplay_server_lifecycle.c -o $(AIRPLAY_SERVER_LIFECYCLE_TEST_BIN)
	@$(AIRPLAY_SERVER_LIFECYCLE_TEST_BIN)

test-c-safety: test-c-size test-soap-writer test-player-types test-seek-target test-iptv-url test-airplay-dns

test-c-safety-sanitize:
	@probe="$${TMPDIR:-/tmp}/nxcast-sanitizer-probe-$$$$"; \
		trap 'rm -f "$$probe" "$$probe.exe"' EXIT HUP INT TERM; \
		if ! printf '%s\n' 'int main(void) { return 0; }' | \
			$(HOST_CC) $(HOST_SANITIZER_FLAGS) -x c - -o "$$probe" >/dev/null 2>&1; then \
			target="$$($(HOST_CC) -dumpmachine 2>/dev/null || printf unknown)"; \
			printf '%s\n' \
				"Host sanitizer link probe failed for $(HOST_CC) ($$target)." \
				"The selected compiler cannot link HOST_SANITIZER_FLAGS='$(HOST_SANITIZER_FLAGS)'." \
				"Install sanitizer runtimes matching that host compiler, or run this target in WSL/Linux with HOST_CC set to a sanitizer-capable compiler." >&2; \
			exit 1; \
		fi
	@ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1 \
		$(MAKE) test-c-safety HOST_CFLAGS="$(HOST_CFLAGS) $(HOST_SANITIZER_FLAGS)"

test-shutdown-order:
	@python3 scripts/test_shutdown_order.py

test-airplay: test-airplay-server-lifecycle test-network-diagnostics test-protocol-coordinator test-dlna-controller-session test-player-actor test-log-mirror test-log-policy test-c-safety test-shutdown-order
	@test "$(HOST_MBEDTLS_FOUND)" = "1" || (printf '%s\n' "mbedTLS 2.x host development files are required (macOS: brew install mbedtls@2)" >&2; exit 1)
	@test "$(HOST_SODIUM_FOUND)" = "1" || (printf '%s\n' "libsodium host development files are required (macOS: brew install libsodium)" >&2; exit 1)
	@test "$(HOST_FFMPEG_FOUND)" = "1" || (printf '%s\n' "FFmpeg host development files are required (macOS: brew install ffmpeg)" >&2; exit 1)
	@mkdir -p $(dir $(AIRPLAY_LIFECYCLE_TEST_BIN))
	$(HOST_CC) $(HOST_CFLAGS) source/protocol/airplay/airplay.c scripts/test_airplay.c -o $(AIRPLAY_LIFECYCLE_TEST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) source/protocol/airplay/protocol/plist.c scripts/test_airplay_plist.c -o $(AIRPLAY_PLIST_TEST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) source/protocol/airplay/protocol/rtsp.c scripts/test_airplay_rtsp.c -o $(AIRPLAY_RTSP_TEST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) -Wframe-larger-than=32768 -c source/protocol/airplay/protocol/rtsp.c -o $(AIRPLAY_RTSP_STACK_CHECK_OBJ)
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_MBEDTLS_CFLAGS) $(HOST_SODIUM_CFLAGS) -DAIRPLAY_CRYPTO_HAVE_ED25519=1 source/protocol/airplay/security/crypto.c source/protocol/airplay/security/identity.c scripts/test_airplay_crypto.c $(HOST_MBEDTLS_LIBS) $(HOST_SODIUM_LIBS) -o $(AIRPLAY_CRYPTO_TEST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_MBEDTLS_CFLAGS) $(HOST_SODIUM_CFLAGS) -DAIRPLAY_CRYPTO_HAVE_ED25519=1 source/protocol/airplay/security/crypto.c source/protocol/airplay/security/srp.c scripts/test_airplay_srp.c $(HOST_MBEDTLS_LIBS) $(HOST_SODIUM_LIBS) -o $(AIRPLAY_SRP_TEST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_MBEDTLS_CFLAGS) $(HOST_SODIUM_CFLAGS) -DAIRPLAY_CRYPTO_HAVE_ED25519=1 -DAIRPLAY_TESTING=1 source/protocol/airplay/protocol/plist.c source/protocol/airplay/protocol/rtsp.c source/protocol/airplay/security/crypto.c source/protocol/airplay/security/identity.c source/protocol/airplay/security/srp.c source/protocol/airplay/security/pairing_store.c source/protocol/airplay/security/pairing.c scripts/test_airplay_pairing.c $(HOST_MBEDTLS_LIBS) $(HOST_SODIUM_LIBS) -o $(AIRPLAY_PAIRING_TEST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_THREAD_FLAGS) $(AIRPLAY_OBSERVABILITY_TEST_FLAGS) $(HOST_MBEDTLS_CFLAGS) source/protocol/airplay/protocol/plist.c source/protocol/airplay/protocol/rtsp.c source/protocol/airplay/security/crypto.c source/protocol/airplay/security/fairplay.c $(PLAYFAIR_SOURCES) source/protocol/airplay/media/remote_video.c source/protocol/airplay/protocol/handlers.c scripts/test_airplay_handlers.c $(HOST_MBEDTLS_LIBS) $(PLAYFAIR_LIBS) -o $(AIRPLAY_HANDLERS_TEST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_MBEDTLS_CFLAGS) source/protocol/airplay/security/crypto.c source/protocol/airplay/security/fairplay.c $(PLAYFAIR_SOURCES) scripts/test_airplay_fairplay.c $(HOST_MBEDTLS_LIBS) $(PLAYFAIR_LIBS) -o $(AIRPLAY_FAIRPLAY_TEST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_THREAD_FLAGS) $(AIRPLAY_OBSERVABILITY_TEST_FLAGS) $(HOST_MBEDTLS_CFLAGS) $(NETWORK_DIAGNOSTICS_SOURCE) $(RUNTIME_DIAGNOSTICS_SOURCE) source/protocol/airplay/security/crypto.c source/protocol/airplay/mirror/video.c source/protocol/airplay/mirror/mirror_session.c scripts/test_airplay_mirror.c $(HOST_MBEDTLS_LIBS) -o $(AIRPLAY_MIRROR_TEST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_THREAD_FLAGS) $(AIRPLAY_OBSERVABILITY_TEST_FLAGS) $(NETWORK_DIAGNOSTICS_SOURCE) $(RUNTIME_DIAGNOSTICS_SOURCE) source/protocol/airplay/mirror/timing.c scripts/test_airplay_timing.c -o $(AIRPLAY_TIMING_TEST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_THREAD_FLAGS) $(HOST_FFMPEG_CFLAGS) source/protocol/airplay/mirror/clock.c source/protocol/airplay/mirror/video.c source/protocol/airplay/media/stream_bridge.c scripts/test_airplay_stream_bridge.c $(HOST_FFMPEG_LIBS) -o $(AIRPLAY_STREAM_BRIDGE_TEST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_THREAD_FLAGS) $(AIRPLAY_OBSERVABILITY_TEST_FLAGS) $(HOST_MBEDTLS_CFLAGS) $(HOST_FFMPEG_CFLAGS) $(NETWORK_DIAGNOSTICS_SOURCE) $(RUNTIME_DIAGNOSTICS_SOURCE) source/protocol/airplay/security/crypto.c source/protocol/airplay/mirror/audio.c source/protocol/airplay/mirror/clock.c source/protocol/airplay/mirror/timing.c source/protocol/airplay/mirror/video.c source/protocol/airplay/mirror/mirror_session.c source/protocol/airplay/media/stream_bridge.c source/protocol/airplay/media/mirror_runtime.c scripts/test_airplay_mirror_runtime.c $(HOST_MBEDTLS_LIBS) $(HOST_FFMPEG_LIBS) -o $(AIRPLAY_MIRROR_RUNTIME_TEST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_THREAD_FLAGS) $(AIRPLAY_OBSERVABILITY_TEST_FLAGS) $(HOST_MBEDTLS_CFLAGS) $(HOST_FFMPEG_CFLAGS) $(NETWORK_DIAGNOSTICS_SOURCE) $(RUNTIME_DIAGNOSTICS_SOURCE) source/protocol/airplay/security/crypto.c source/protocol/airplay/mirror/audio.c source/protocol/airplay/mirror/clock.c source/protocol/airplay/mirror/video.c source/protocol/airplay/media/stream_bridge.c scripts/test_airplay_audio.c $(HOST_MBEDTLS_LIBS) $(HOST_FFMPEG_LIBS) -o $(AIRPLAY_AUDIO_TEST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) source/protocol/airplay/mirror/clock.c scripts/test_airplay_clock.c -o $(AIRPLAY_CLOCK_TEST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_THREAD_FLAGS) source/protocol/airplay/protocol/plist.c source/protocol/airplay/protocol/rtsp.c source/protocol/airplay/media/remote_video.c scripts/test_airplay_remote_video.c -o $(AIRPLAY_REMOTE_VIDEO_TEST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_THREAD_FLAGS) source/player/core/ownership.c scripts/test_player_ownership.c -o $(PLAYER_OWNERSHIP_TEST_BIN)
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_THREAD_FLAGS) $(NETWORK_DIAGNOSTICS_SOURCE) source/protocol/airplay/protocol/rtsp.c source/protocol/airplay/server.c scripts/airplay_smoke_server.c -o $(AIRPLAY_SMOKE_SERVER_BIN)
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_THREAD_FLAGS) $(HOST_MBEDTLS_CFLAGS) $(HOST_SODIUM_CFLAGS) -DAIRPLAY_CRYPTO_HAVE_ED25519=1 $(NETWORK_DIAGNOSTICS_SOURCE) source/protocol/airplay/protocol/plist.c source/protocol/airplay/protocol/rtsp.c source/protocol/airplay/server.c source/protocol/airplay/security/crypto.c source/protocol/airplay/security/identity.c source/protocol/airplay/security/srp.c source/protocol/airplay/security/pairing_store.c source/protocol/airplay/security/pairing.c scripts/airplay_pairing_smoke_server.c $(HOST_MBEDTLS_LIBS) $(HOST_SODIUM_LIBS) -o $(AIRPLAY_PAIRING_SMOKE_SERVER_BIN)
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_THREAD_FLAGS) -DAIRPLAY_TESTING=1 $(NETWORK_DIAGNOSTICS_SOURCE) source/protocol/airplay/discovery/dns.c source/protocol/airplay/discovery/mdns.c scripts/airplay_mdns_smoke_server.c -o $(AIRPLAY_MDNS_SMOKE_SERVER_BIN)
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_THREAD_FLAGS) $(HOST_MBEDTLS_CFLAGS) $(HOST_SODIUM_CFLAGS) -DAIRPLAY_CRYPTO_HAVE_ED25519=1 $(NETWORK_DIAGNOSTICS_SOURCE) source/protocol/airplay/airplay.c source/protocol/airplay/protocol/plist.c source/protocol/airplay/protocol/rtsp.c source/protocol/airplay/media/remote_video.c source/protocol/airplay/protocol/handlers.c source/protocol/airplay/security/crypto.c source/protocol/airplay/security/identity.c source/protocol/airplay/security/srp.c source/protocol/airplay/security/pairing_store.c source/protocol/airplay/security/pairing.c source/protocol/airplay/security/fairplay.c $(PLAYFAIR_SOURCES) source/protocol/airplay/discovery/dns.c source/protocol/airplay/discovery/mdns.c source/protocol/airplay/server.c source/protocol/airplay/receiver.c scripts/airplay_receiver_smoke_server.c $(HOST_MBEDTLS_LIBS) $(HOST_SODIUM_LIBS) $(PLAYFAIR_LIBS) -o $(AIRPLAY_RECEIVER_SMOKE_SERVER_BIN)
	@$(AIRPLAY_LIFECYCLE_TEST_BIN)
	@$(AIRPLAY_PLIST_TEST_BIN)
	@$(AIRPLAY_RTSP_TEST_BIN)
	@$(AIRPLAY_CRYPTO_TEST_BIN)
	@$(AIRPLAY_SRP_TEST_BIN)
	@$(AIRPLAY_PAIRING_TEST_BIN)
	@$(AIRPLAY_HANDLERS_TEST_BIN)
	@$(AIRPLAY_FAIRPLAY_TEST_BIN)
	@$(AIRPLAY_MIRROR_TEST_BIN)
	@$(AIRPLAY_TIMING_TEST_BIN)
	@$(AIRPLAY_STREAM_BRIDGE_TEST_BIN)
	@$(AIRPLAY_MIRROR_RUNTIME_TEST_BIN)
	@$(AIRPLAY_AUDIO_TEST_BIN)
	@$(AIRPLAY_CLOCK_TEST_BIN)
	@$(AIRPLAY_REMOTE_VIDEO_TEST_BIN)
	@$(PLAYER_OWNERSHIP_TEST_BIN)
	@python3 scripts/smoke_airplay.py --port 0
	@python3 scripts/smoke_airplay_pairing.py --port 0
	@python3 scripts/smoke_airplay.py --mdns
	@python3 scripts/smoke_airplay.py --receiver --port 0
	@python3 scripts/smoke_airplay.py --remote-hls


#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
ifeq ($(strip $(APP_JSON)),)

all	:	$(OUTPUT).nro

ifeq ($(strip $(NO_NACP)),)
$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp
else
$(OUTPUT).nro	:	$(OUTPUT).elf
endif

else

all	:	$(OUTPUT).nsp

$(OUTPUT).nsp	:	$(OUTPUT).nso $(OUTPUT).npdm

$(OUTPUT).nso	:	$(OUTPUT).elf

endif

$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES_BIN)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
