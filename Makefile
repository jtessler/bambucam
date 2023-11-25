PLUGIN_PATH ?= $(HOME)/.config/BambuStudio/plugins
OBJECTS := bambucam.o
LDFLAGS := \
		-lBambuSource -L$(PLUGIN_PATH) -Wl,-rpath=$(PLUGIN_PATH) \
		-lavcodec \
		-lavformat \
		-lavutil \

bambucam: $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	@rm -fv $(OBJECTS) bambucam
