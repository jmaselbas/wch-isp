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
        --port=USB   Specify port as USB (default)
        --port=/dev/ttyUSB0      Specify port as COM-port '/dev/ttyUSB0'
        --port='//./COM3'        Specify port as COM-port '//./COM3'
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
```

This utility has been tested on:
 - CH32V307


## Examples

List detected device in bootloader mode:

```sh
$ ./wch-isp list
found bt ver 0209 uid = [87-80-CB-26-3B-38-8D-DF]
```

Flash the `firmware.bin` file via USB, `-p` enable the progress.

```
$ ./wch-isp --port=USB -p write firmware.bin
file size = 792
Erase 1 sectors (1024 bytes)
Write: 100.0 %   Write 792 bytes: DONE
Verify: 100.0 %   Verify 792 bytes: DONE
```

Verify the `firmware.hex` file via COM-port (reset connected to RTS, Boot0 connected to DTR):

```
$ ./wch-isp --port=/dev/ttyUSB0 --reset=RTS --boot0=DTR verify firmware.hex
file size = 792
Verify 792 bytes: DONE

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

## TODO:

- Add database to read flash size, avaible option bytes values etc.

- Add erase function (requires database)

- Add option bytes write function (requires database)

- Test compilling on Windows (Jules Maselbas probably tested it, but I (COKPOWEHEU) haven't yet)
