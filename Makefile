# SPDX-License-Identifier: GPL-2.0-only
VERSION = 0.1.0

# Install paths
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man
UDEVPREFIX = /etc/udev

#make CROSS_COMPILE=i686-w64-mingw32- INCS="-Imingw32/include -Imingw32/include/libusb-1.0" LIBS="-Lmingw32/lib mingw32/bin/libusb-1.0.dll -lyaml"
ifneq ($(CROSS_COMPILE),)
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)ld
endif

PKG_CONFIG = pkg-config

# include and libs
INCS += `$(PKG_CONFIG) --cflags libusb-1.0`  `$(PKG_CONFIG) --cflags yaml-0.1`
LIBS += `$(PKG_CONFIG) --libs libusb-1.0`  `$(PKG_CONFIG) --libs yaml-0.1`

# Flags
WCHISP_CPPFLAGS = -DVERSION=\"$(VERSION)\" $(CPPFLAGS)
WCHISP_CFLAGS = -Wall -O2 $(INCS) $(CFLAGS)
WCHISP_LDFLAGS = $(LIBS) $(LDFLAGS)

SRC = main.c wch_if_usb.c wch_if_uart.c wch_yaml_parse.c
#SRC = wch-isp.c
HDR = arg.h devices.h
OBJ = $(SRC:.c=.o)
BIN = wch-isp
MAN = wch-isp.1
DISTFILES = $(SRC) $(HDR) $(MAN) 50-wchisp.rules Makefile

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) -o $@ $^ $(WCHISP_LDFLAGS)
.c.o:
	$(CC) $(WCHISP_CFLAGS) $(WCHISP_CPPFLAGS) -c $<

install:
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f $(BIN) $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(BIN)
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < $(MAN) > $(DESTDIR)$(MANPREFIX)/man1/$(MAN)
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/$(MAN)

install-rules:
	mkdir -p $(DESTDIR)$(UDEVPREFIX)/rules.d
	cp -f 50-wchisp.rules $(DESTDIR)$(UDEVPREFIX)/rules.d

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)
	rm -f $(DESTDIR)$(MANPREFIX)/man1/$(MAN)

dist:
	mkdir -p $(BIN)-$(VERSION)
	cp $(DISTFILES) $(BIN)-$(VERSION)
	tar -cf $(BIN)-$(VERSION).tar $(BIN)-$(VERSION)
	gzip $(BIN)-$(VERSION).tar
	rm -rf $(BIN)-$(VERSION)

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all install install-rules uninstall dist clean
