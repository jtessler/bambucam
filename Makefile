OBJECTS := bambucam.o
LDFLAGS := -lBambuSource -L$(PWD) -Wl,-rpath=$(PWD)

bambucam: $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	@rm -fv $(OBJECTS) bambucam
