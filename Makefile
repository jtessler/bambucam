PLUGIN_PATH ?= $(HOME)/.config/BambuStudio/plugins

LDFLAGS := \
		-lBambuSource -L$(PLUGIN_PATH) -Wl,-rpath=$(PLUGIN_PATH) \
		-lavcodec \
		-lavformat \
		-lavutil \

OBJECTS := \
	bambucam.o \
	bambucam_impl.o \

bambucam: $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	@rm -fv $(OBJECTS) bambucam
