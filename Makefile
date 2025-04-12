# Add more verbose logging and build debug symbols (if set).
DEBUG ?=

# Use a fake camera implementation for testing (if set).
BAMBU_FAKE ?=

# Path to the Bambu Studio plugin directory. Assumes it is installed and ran at
# least once to download the expected plugins.
PLUGIN_PATH ?= $(HOME)/.config/BambuStudio/plugins

# Which server implementation to use, including:
# - HTTP: Multipart JPEG stream using microhttpd
# - RTP:  RTP video stream using FFmpeg
SERVER ?= HTTP

ifdef DEBUG
CFLAGS := -g -DDEBUG
endif

LDFLAGS := \
	-L$(PLUGIN_PATH) \
	-Wl,-rpath=$(PLUGIN_PATH) \

LDLIBS := -lpthread

OBJECTS :=

ifdef BAMBU_FAKE
	LDLIBS  += -ljpeg
	OBJECTS += bambu_fake.o
else
	LDLIBS  += -lBambuSource
	OBJECTS += bambu.o
endif

ifeq ($(SERVER), HTTP)
	LDLIBS  += -lmicrohttpd
	OBJECTS += server_microhttpd.o
else
	LDLIBS  += -lavcodec -lavformat -lavutil
	OBJECTS += server_ffmpeg_rtp.o
endif

bambucam: $(OBJECTS)

.PHONY: clean
clean:
	@rm -fv *.o
	@rm -fv bambucam
