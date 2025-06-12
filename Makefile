# Add more verbose logging and build debug symbols (if set).
DEBUG ?=

# Use a fake camera implementation for testing (if set).
BAMBU_FAKE ?=

USER_CONFIG_DIR ?= $(HOME)/.config
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	USER_CONFIG_DIR := '$(HOME)/Library/Application Support'
endif
# Path to the Bambu Studio plugin directory. Assumes it is installed and ran at
# least once to download the expected plugins.
PLUGIN_PATH := $(USER_CONFIG_DIR)/BambuStudio/plugins

# Which server implementation to use, including:
# - HTTP: Multipart JPEG stream using microhttpd
# - RTP:  RTP video stream using FFmpeg
SERVER ?= HTTP

ifdef DEBUG
CFLAGS := -g -DDEBUG
endif

LDLIBS := -lpthread

OBJECTS :=

ifdef BAMBU_FAKE
	CFLAGS += $(shell pkg-config --cflags libjpeg)
	LDLIBS  += $(shell pkg-config --libs libjpeg)
	OBJECTS += bambu_fake.o
else
	LDFLAGS := \
		-L$(PLUGIN_PATH) \
		-Wl,-rpath,$(PLUGIN_PATH)
	LDLIBS  += -lBambuSource
	OBJECTS += bambu.o
endif

ifeq ($(SERVER), HTTP)
	CFLAGS += $(shell pkg-config --cflags libmicrohttpd)
	LDLIBS  += $(shell pkg-config --libs libmicrohttpd)
	OBJECTS += server_microhttpd.o
else
	CFLAGS += $(shell pkg-config --cflags libavcodec libavformat libavutil)
	LDLIBS  += $(shell pkg-config --libs libavcodec libavformat libavutil)
	OBJECTS += server_ffmpeg_rtp.o
endif

bambucam: $(OBJECTS)

.PHONY: clean
clean:
	@rm -fv *.o
	@rm -fv bambucam
