wch-isp
=======

wch-isp is an utility to write firmware into the flash of WCH microcontrollers, over USB.
This utility started as a rewrite in C of the rust tool [wchisp](https://github.com/ch32-rs/wchisp).

This utility has been tested on:
 - CH32V103
 - CH569W

## Dependency

wch-isp depends on libusb 1.0 or above.

## How to build

### Linux

You can, optionally, modify the Makefile to match your local setup.
By default wch-isp will be installed in `/usr/local/bin` and udev rules (if installed) will go in `/etc/udev/rules.d`.

Afterwards enter the following commands to build and install wch-isp:
```
make
make install
```

wch-isp will likely require udev rules to have access to the USB bootloader.
Default udev rules are provided and can be installed with this command:
```
make install-rules
```

### Windows using MSYS2

On Windows the build is done using MSYS2 and mingw64, you can install this from https://www.msys2.org
Then from the MSYS2 console install the dependencies:
```
pacman -S mingw-w64-x86_64-make mingw-w64-x86_64-pkgconf mingw-w64-x86_64-gcc mingw-w64-x86_64-libusb
```

Then build using make:
```
PKG_CONFIG_PATH="$PKG_CONFIG_PATH:/mingw64/lib/pkgconfig" make
```

Then the `wch-isp.exe` binary can be run like so:
```
PATH="$PATH:/mingw64/bin" ./wch-isp.exe
```
