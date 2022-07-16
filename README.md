wch-isp
=======

wch-isp is a small utility to program WCH micro-controllers.
This utility is a rewrite in C of the rust tool [wchisp](https://github.com/ch32-rs/wchisp).
This utility has only be tested on the CH32V103 & CH569W but should work on other products by WCH.

# MounRiver Studio or MounRiver Studio Community project settings to generate bin file for CH56x...
## Properties of project => C/C++ Build => Settings => Build Steps
### Post-build steps:
riscv-none-embed-objcopy -O binary "${ProjName}.elf"  "${ProjName}.bin"
### Description:
Create Flash Image BIN

# Driver installation for Windows:
Run Zadig (executable can be found on https://zadig.akeo.ie/)
Install or Reinstall driver for "USB Module" (with USB ID 4348 55E0) with libusb-win32 (v1.2.6.0)
Note: After 10s without any activity the device "USB Module" (with USB ID 4348 55E0) will disappear as the bootloader timeout so it shall be restarted
