CC      ?= cc
CFLAGS  ?= -O2 -g
PREFIX  ?= /usr/local
LIBDIR  ?= $(PREFIX)/lib

LIB      = libalsa-time64.so
FLAGS    = -Wall -Wextra -D_FILE_OFFSET_BITS=64 -D_TIME_BITS=64

all: $(LIB)

$(LIB): alsa-time64.c
	$(CC) $(CFLAGS) $(FLAGS) -fPIC -shared -o $@ $< -ldl $(LDFLAGS)

install: $(LIB)
	install -Dm755 $(LIB) $(DESTDIR)$(LIBDIR)/$(LIB)

uninstall:
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB)

clean:
	rm -f $(LIB)

.PHONY: all install uninstall clean
