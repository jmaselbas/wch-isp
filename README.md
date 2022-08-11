wch-isp
=======

wch-isp is a small utility to program WCH micro-controllers.
This utility is a rewrite in C of the rust tool [wchisp](https://github.com/ch32-rs/wchisp).
This utility has only be tested on the CH32V103 & CH569W but should work on other products by WCH.

### MounRiver Studio or MounRiver Studio Community project settings to generate bin file for CH56x...
 - #### Properties of project => C/C++ Build => Settings => Build Steps
   - ##### Post-build steps:
     - `riscv-none-embed-objcopy -O binary "${ProjName}.elf"  "${ProjName}.bin"`
   - ##### Description:
     - `Create Flash Image BIN`

### Driver installation for Windows
- Run Zadig (executable can be found on https://zadig.akeo.ie/)
- Install or Reinstall driver for "USB Module" (with USB ID `4348` `55E0`) with `libusb-win32` or `WinUSB`
- Note: After 10s without any activity the device "USB Module" (with USB ID `4348` `55E0`) will disappear as the bootloader timeout so it shall be restarted

### How to build wch-isp
- See the document [How_To_Build.md](How_To_Build.md)

### How to use wch-isp
- Start a shell(on GNU/Linux) or execute cmd(on Windows)
- For usage type wch-isp -h (options -p display progress, -v do verify after write, -r reset board at end, -V print the version) 
  - #### GNU/Linux
    - wch-isp -vr flash fullpath/file.bin
  - #### Windows
    - wch-isp.exe -vr flash fullpath/file.bin

### CH569 Debug mode
* If the CH569 config `debug` is `on/enabled` it is impossible to flash a program with `wch-isp` and it will return error "Fail to program chunk @ 0 error: e0 00" (the booloader refuse to flash anything when debug mode is on/enabled).
* To check if the CH569 config `debug` state launch `wch-isp -c` and it will display the config of the CH569 including debug state (enabled or disabled)
* To disable the debug mode launch `wch-isp debug-off`

Example WCH569 config with debug enabled/on:
```
nv=0x8FFFF2E5
[4] RESET_EN 0: disabled
[5] DEBUG_EN 1: enabled
[6] BOOT_EN 1: enabled
[7] CODE_READ_EN 1: enabled
[29] LOCKUP_RST_EN 0: disabled
[31:30] USER_MEM 0x02: RAMX 96KB + ROM 32KB
```

Example WCH569 config with debug disabled/off:
```
nv=0x8FFFF245
[4] RESET_EN 0: disabled
[5] DEBUG_EN 0: disabled
[6] BOOT_EN 1: enabled
[7] CODE_READ_EN 0: disabled
[29] LOCKUP_RST_EN 0: disabled
[31:30] USER_MEM 0x02: RAMX 96KB + ROM 32KB
```
