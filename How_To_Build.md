### How To Build

GitHub Workflows / CI Build is available on [Build.yml](.github/workflows/Build.yml)

###  GNU/Linux Ubuntu 20.04 LTS
#### Prerequisites
`sudo apt install pkg-config gcc libusb-1.0-0-dev`
#### Automatic build (using Makefile) 
`make`

### Windows MSYS2/mingw64
#### Prerequisites
- install MSYS2/mingw64 from https://www.msys2.org
- Start mingw64 console
- `pacman -Syu`
- `pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-libusb`
#### Automatic build (using Makefile)
`ming32-make.exe`
- Optional copy libusb-1.0.dll to same directory as the executable(if it is not in your path)
  - `cp /mingw64/bin/libusb-1.0.dll ./`
