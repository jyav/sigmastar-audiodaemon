# Variables
commit_tag=$(shell git rev-parse --short HEAD)

# -----------------------------------------------------------------------------
# OPENIPC / BUILDROOT CONFIGURATION
# Note: CC, CFLAGS, LDFLAGS, and STRIP are automatically provided by Buildroot 
# via TARGET_CONFIGURE_OPTS. We only append our daemon-specific requirements.
# -----------------------------------------------------------------------------

SDK_INC_DIR = include

INCLUDES += -I$(SDK_INC_DIR) \
            -I./src/iad/network \
            -I./src/iad/audio \
            -I./src/iac/client \
            -I./src/iad/utils \
            -I./build \
            -I$(SDK_INC_DIR)/libwebsockets

CFLAGS += $(INCLUDES)

ifeq ($(DEBUG), y)
    CFLAGS += -g
    STRIPCMD = @echo "Not stripping binary due to DEBUG mode."
else
    # Fallback to standard strip if not explicitly defined by Buildroot
    STRIP ?= strip
    STRIPCMD = $(STRIP)
endif

# SigmaStar Hardware Libraries
SSTARLDLIBS = -lmi_ao -lmi_ai -lmi_sys -lmi_common

# Standard Linux Libraries
LDLIBS = -lpthread -lm -lrt -ldl -lcjson -lwebsockets

# -----------------------------------------------------------------------------
# TARGETS & OBJECT FILES
# -----------------------------------------------------------------------------
AUDIO_PROGS = build/bin/audioplay build/bin/iad build/bin/iac build/bin/wc-console build/bin/web_client

# Removed build/obj/audio/audio_imp.o
iad_OBJS = build/obj/iad.o \
           build/obj/audio/output.o \
           build/obj/audio/input.o \
           build/obj/audio/audio_common.o \
           build/obj/network/network.o \
           build/obj/network/control_server.o \
           build/obj/network/input_server.o \
           build/obj/network/output_server.o \
           build/obj/utils/utils.o \
           build/obj/utils/logging.o \
           build/obj/utils/config.o \
           build/obj/utils/cmdline.o

iac_OBJS = build/obj/iac.o \
           build/obj/client/cmdline.o \
           build/obj/client/client_network.o \
           build/obj/client/playback.o \
           build/obj/client/record.o

web_client_OBJS = build/obj/web_client.o \
                  build/obj/web_client_src/cmdline.o \
                  build/obj/web_client_src/client_network.o \
                  build/obj/web_client_src/playback.o \
                  build/obj/web_client_src/utils.o

audioplay_OBJS = build/obj/standalone/audioplay.o

wc_console_OBJS = build/obj/wc-console/wc-console.o

# -----------------------------------------------------------------------------
# BUILD RULES
# -----------------------------------------------------------------------------
.PHONY: all version clean distclean audioplay wc-console web_client

all: version $(AUDIO_PROGS)

BUILD_DIR = build
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

version: $(BUILD_DIR)
	@if ! grep "$(commit_tag)" build/version.h >/dev/null 2>&1 ; then \
		echo "update version.h" ; \
		sed 's/COMMIT_TAG/"$(commit_tag)"/g' config/version.tpl.h > build/version.h ; \
	fi

deps:
	./scripts/deps.sh deps $(PLATFORM)
	./scripts/make_libwebsockets_deps.sh
	./scripts/make_cJSON_deps.sh

dependancies: deps

build/obj/%.o: ingenic_musl/%.c
	$(CC) $(CFLAGS) -c $< -o $@

build/obj/%.o: src/common/%.c
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $< -o $@

build/obj/%.o: src/iad/%.c
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $< -o $@

build/obj/%.o: src/iac/%.c
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $< -o $@

build/obj/%.o: src/web_client/%.c
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $< -o $@

build/obj/standalone/%.o: src/standalone/%.c
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $< -o $@

build/obj/wc-console/%.o: src/wc-console/%.c
	@mkdir -p $(@D)
	$(CC) -c $(CFLAGS) $< -o $@

# Main Daemon Linking (Replaced IMPLDLIBS with SSTARLDLIBS)
iad: build/bin/iad
build/bin/iad: version $(iad_OBJS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $(iad_OBJS) $(SSTARLDLIBS) $(LDLIBS)
	$(STRIPCMD) $@

iac: build/bin/iac
build/bin/iac: version $(iac_OBJS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $(iac_OBJS) $(LDLIBS)
	$(STRIPCMD) $@

audioplay: build/bin/audioplay
build/bin/audioplay: version $(audioplay_OBJS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $(audioplay_OBJS) $(SSTARLDLIBS) $(LDLIBS)
	$(STRIPCMD) $@

wc-console: build/bin/wc-console
build/bin/wc-console: version $(wc_console_OBJS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $(wc_console_OBJS) $(LDLIBS)
	$(STRIPCMD) $@

web_client: build/bin/web_client
build/bin/web_client: version $(web_client_OBJS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $(web_client_OBJS) $(LDLIBS)
	$(STRIPCMD) $@

clean:
	-find build/obj -type f -name "*.o" -exec rm {} \;
	-rm -f build/version.h

distclean: clean
	-rm -f $(AUDIO_PROGS)
	-rm -rf build/*
	-rm -f lib/libwebsockets.* include/lws_config.h include/libwebsockets.h lib/libcjson.so
	-rm -rf include/libwebsockets
