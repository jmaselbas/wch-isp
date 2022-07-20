# SPDX-License-Identifier: GPL-2.0-only
VERSION = 0.1

# Install paths
PREFIX = /usr/local

ifneq ($(CROSS_COMPILE),)
CC = $(CROSS_COMPILE)cc
LD = $(CROSS_COMPILE)ld
endif

PKG_CONFIG = pkg-config

# include and libs
INCS = `$(PKG_CONFIG) --cflags libusb-1.0`
LIBS = `$(PKG_CONFIG) --libs libusb-1.0`

# Flags
CPPFLAGS = -DVERSION=\"$(VERSION)\"
CFLAGS = -Wall -O2 $(INCS)
LDFLAGS = $(LIBS)

SRC = wch-isp.c
HDR = arg.h devices.h
OBJ = $(SRC:.c=.o)
BIN = wch-isp
DISTFILES = $(SRC) $(HDR) Makefile

all: $(BIN)

$(OBJ): arg.h devices.h

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

install:
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f $(BIN) $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(BIN)

uninstall:
	rm -vf $(DESTDIR)$(PREFIX)/bin/$(BIN)

dist:
	mkdir -p $(BIN)-$(VERSION)
	cp $(DISTFILES) $(BIN)-$(VERSION)
	tar -cf $(BIN)-$(VERSION).tar $(BIN)-$(VERSION)
	gzip $(BIN)-$(VERSION).tar
	rm -rf $(BIN)-$(VERSION)

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all install uninstall dist clean
