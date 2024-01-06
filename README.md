wch-isp
=======

wch-isp is an utility to write firmware into the flash of WCH microcontrollers, over USB.
This utility started as a rewrite in C of the rust tool [wchisp](https://github.com/ch32-rs/wchisp).

```
usage: wch-isp [-VDnpr] [-d <uid>] COMMAND [ARG ...]
       wch-isp [-VDnpr] [-d <uid>] [flash|write|verify|reset] FILE
       wch-isp [-VDnpr] [-d <uid>] [erase|config|remove-wp]
       wch-isp [-VDnpr] [list]

options:
  -d <uid> Select the usb device that matches by uid first, else by index
  -n       No verify after writing to flash, done by default
  -p       Print a progress-bar during command operation
  -r       Reset after command completed
  -D       Print raw isp command (for debug)
  -V       Print version and exit
```

This utility has been tested on:
 - CH32V103
 - CH569W
 - CH32V307VCT6 (Board YD-CH32V307VCT6)


## Examples

List detected device in bootloader mode:
```sh
$ wch-isp list
0: BTVER v2.7 UID 8d-ff-ba-e4-c2-84-09-69 [0x1069] CH569 (flash 448K)
1: BTVER v2.5 UID f2-3e-88-26-3b-38-b5-9d [0x1980] CH32V208WB (flash 128K)
2: BTVER v2.6 UID cd-ab-72-86-45-bc-84-ee [0x1931] CH32V203C8 (flash 64K)
```

Flash the `firmware.bin` file, `-p` enable the progress bar.
```
$ wch-isp -p flash firmware.bin
BTVER v2.5 UID f2-3e-88-26-3b-38-b5-9d [0x1980] CH32V208WB (flash 128K)
[####################################################] write 35392/35392
[####################################################] verify 35392/35392
flash done
```

Erase the device's flash, select the device by it's uid (option `-d`).
```
$ wch-isp -d f2-3e-88-26-3b-38-b5-9d erase
BTVER v2.5 UID f2-3e-88-26-3b-38-b5-9d [0x1980] CH32V208WB (flash 128K)
erase done
```

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
