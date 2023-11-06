PLUGIN_PATH ?= $(HOME)/.config/BambuStudio/plugins
OBJECTS := bambucam.o
LDFLAGS := -lBambuSource -L$(PLUGIN_PATH) -Wl,-rpath=$(PLUGIN_PATH)

bambucam: $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	@rm -fv $(OBJECTS) bambucam
