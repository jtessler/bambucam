DEBUG ?=
PLUGIN_PATH ?= $(HOME)/.config/BambuStudio/plugins

ifdef DEBUG
CFLAGS := -g -DDEBUG
endif

LDFLAGS := \
	-L$(PLUGIN_PATH) \
	-Wl,-rpath=$(PLUGIN_PATH) \

LDLIBS := \
	-lBambuSource \
	-lavcodec \
	-lavformat \
	-lavutil \
	-lpthread \

OBJECTS := \
	bambu.o \
	server_ffmpeg_rtp.o \

bambucam: $(OBJECTS)

.PHONY: clean
clean:
	@rm -fv $(OBJECTS) bambucam
