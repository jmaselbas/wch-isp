# How To Build

# GNU/Linux Ubuntu 20.04 LTS
## Prerequisites
sudo apt install pkg-config gcc libusb-1.0-0-dev
## Automatic build (using Makefile) 
make
## Manual build
gcc -Wall -O3 -I/usr/include/libusb-1.0 -o wch-isp wch-isp.c -lusb-1.0 -DVERSION=\"0.0.2\"

# Windows MSYS2/mingw64 (https://www.msys2.org)
## Prerequisites
pacman -Syu
pacman -S pkg-config make mingw-w64-x86_64-gcc mingw-w64-x86_64-libusb
## Automatic build (using Makefile)
ming32-make.exe
## Manual build 
gcc -Wall -O3 -L/mingw64/lib -I/mingw64/include/libusb-1.0 -o wch-isp.exe wch-isp.c -lusb-1.0 -DVERSION=\"0.0.2\"
