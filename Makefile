CC      ?= cc
CFLAGS  ?= -O2 -g
PREFIX  ?= /usr/local
LIBDIR  ?= $(PREFIX)/lib

# Set RUNNER to run the tests through an emulator, e.g.
#   make check CC=arm-linux-gnueabihf-gcc RUNNER="qemu-arm -L /usr/arm-linux-gnueabihf"
RUNNER  ?=

LIB      = libalsa-time64.so
FLAGS    = -Wall -Wextra -D_FILE_OFFSET_BITS=64 -D_TIME_BITS=64
TESTLIBS = tests/libfake-kernel.so

all: $(LIB)

$(LIB): alsa-time64.c
	$(CC) $(CFLAGS) $(FLAGS) -fPIC -shared -o $@ $< -ldl $(LDFLAGS)

tests/libfake-kernel.so: tests/fake-kernel.c tests/fake-kernel.h
	$(CC) $(CFLAGS) $(FLAGS) -fPIC -shared -o $@ $<

# The shim is linked ahead of the fake kernel, so its ioctl() is the one the
# test calls and the fake kernel's is the next one (RTLD_NEXT).
tests/test-shim: tests/test-shim.c $(LIB) $(TESTLIBS)
	cp $(LIB) tests/
	$(CC) $(CFLAGS) $(FLAGS) -o $@ $< -Ltests -lalsa-time64 -lfake-kernel \
		-Wl,-rpath,'$$ORIGIN' -Wl,--no-as-needed

tests/test-baseline: tests/test-shim.c $(TESTLIBS)
	$(CC) $(CFLAGS) $(FLAGS) -o $@ $< -Ltests -lfake-kernel -Wl,-rpath,'$$ORIGIN'

check: tests/test-shim tests/test-baseline
	$(RUNNER) tests/test-baseline baseline
	$(RUNNER) tests/test-shim old
	FAKE_KERNEL_TIME64=1 $(RUNNER) tests/test-shim new

install: $(LIB)
	install -Dm755 $(LIB) $(DESTDIR)$(LIBDIR)/$(LIB)

uninstall:
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB)

clean:
	rm -f $(LIB) tests/*.so tests/test-shim tests/test-baseline

.PHONY: all check install uninstall clean
