wch-isp
=======

wch-isp is an utility to write firmware into the flash of WCH microcontrollers, over USB or COM-port.
This utility started as a rewrite in C of the rust tool [wchisp](https://github.com/ch32-rs/wchisp).

```
usage: ./wch-isp [OPTIONS] COMMAND [ARG...]
  OPTIONS:
        -v           Print version and exit
        -h, --help   Show this help and exit
        -n           No verify after writing
        -p           Show progress bar
        -d           Debug mode, show raw commands
        -r           Reset after command completed
        -b           Do not read database\n");
        -f           Ignore if firmware size more than cached flash size (program memory)
        -F           Ignore if firmware size more than total flash size (program memory + const data memory
        --port=USB   Specify port as USB (default)
        --port=/dev/ttyUSB0      Specify port as COM-port '/dev/ttyUSB0'
        --port='//./COM3'        Specify port as COM-port '//./COM3'
        --device=DEV Test if connected device is DEV and exit if they differ
        --uid=AA-BB-CC-DD-EE-FF-GG-HH   Specify device UID
        --reset=PIN  Use PIN as RESET
        --boot0=PIN  Use PIN as Boot0
            'PIN' may be 'RTS', 'DTR', 'nRTS' or 'nDTR'
        --address=0x08000000     Write or verify data from specified address

  COMMAND:
        write FILE    write file (.hex or .bin)
        verify FILE   verify file (.hex ot .bin)
        erase         erase all memory
        list          show connected devices
        unlock        remove write protection
        info          show device info: bootloader version, option bytes, flash size etc
        optionbytes 'CMD' change optionbytes
            example: ./wch-isp optionbytes 'RDPR=0xA5, DATA0 = 0x42'
        optionshow 'CMD'  show changes after apply CMD to optionbytes; Do not write
```

This utility has been tested on:
 - CH32V307RCT6
 - CH32V203G8R6


## Examples

List detected device in bootloader mode:

```sh
./wch-isp list
found 0x19 0x3B ( CH32V203G8R6 ), bt ver.0206 uid = [ CD-AB-1D-36-51-BC-3B-9E ]
found 0x17 0x71 ( CH32V307RCT6 ), bt ver.0209 uid = [ 87-80-CB-26-3B-38-8D-DF ]
```

Flash the `firmware.bin` file via USB, `-p` enable the progress.

```sh
$ ./wch-isp --port=USB -p write firmware.bin
Erase 1 sectors (1024 bytes)
Write: 100.0 %   Write 792 bytes: DONE
Verify: 100.0 %   Verify 792 bytes: DONE
```

Verify the `firmware.hex` file via COM-port (reset connected to RTS, Boot0 connected to DTR):

```sh
$ ./wch-isp --port=/dev/ttyUSB0 --reset=RTS --boot0=DTR verify firmware.hex
Verify 792 bytes: DONE

```

Unlock read-protection and write 0x42 to DATA0 field in optionbytes:

```sh
$ ./wch-isp --device=CH32V203G8R6 optionbytes 'RDPR=0xA5 DATA0 = 0x42'
Option bytes write:
  0xC03F5AA5
  0xBA45BD42
  0xFFFFFFFF
Done

```

## Dependency

wch-isp depends on **libusb-1.0** and **yaml-0.1** or above.

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

### Cross-compilling using mingw32

Download and unarchive **mingw-w64-i686-libusb** and **mingw-w64-i686-libyaml** into special directory, for example, ```lib/mingw32```. Then execute makefile:

```
make CROSS_COMPILE=i686-w64-mingw32- INCS="-Imingw32/include -Imingw32/include/libusb-1.0" LIBS="-Lmingw32/lib mingw32/bin/libusb-1.0.dll -lyaml"
```

Comilled binary will reqire **libusb-1.0.dll**, **libyaml-0-2.dll** (from ```lib/mingw32/bin```) and **libgcc_s_dw2-1.dll** (in my system: ```/usr/lib/gcc/i686-w64-mingw32/10-win32/libgcc_s_dw2-1.dll```)

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

## TODO:

- Test compilling on Windows (Jules Maselbas probably tested it, but I (COKPOWEHEU) haven't yet)
