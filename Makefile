# SPDX-License-Identifier: GPL-2.0-only
SRC = wch-isp.c
BIN = wch-isp

# Flags
CFLAGS = -Wall -O2 `pkg-config --cflags libusb-1.0`
LDFLAGS = `pkg-config --libs libusb-1.0`

$(BIN): arg.h
all: $(BIN)

clean:
	rm $(BIN)
