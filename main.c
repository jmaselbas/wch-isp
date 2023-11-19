#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include "wch_if.h"
//TODO: change to reading YAML database from wch-isp-rust
//#include "devices.h"

/*
 *  All readable and writable registers.
 *  - `RDPR`: Read Protection
 *  - `USER`: User Config Byte (normally in Register Map datasheet)
 *  - `WPR`:  Write Protection Mask, 1=unprotected, 0=protected
 *
 *  | BYTE0  | BYTE1  | BYTE2  | BYTE3  |
 *  |--------|--------|--------|--------|
 *  | RDPR   | nRDPR  | USER   | nUSER  |
 *  | DATA0  | nDATA0 | DATA1  | nDATA1 |
 *  | WPR0   | WPR1   | WPR2   | WPR3   |
 */
#define CFG_MASK_RDPR_USER_DATA_WPR 0b111
#define CFG_MASK_BTVER 0b1000 // Bootloader version, in the format of `[0x00, major, minor, 0x00]`
#define CFG_MASK_UID  0b10000 // Device Unique ID
#define CFG_MASK_ALL  0b11111 // All mask bits of CFGs


#define CMD_IDENTIFY	0xA1
#define CMD_ISP_END 	0xA2
#define CMD_ISP_KEY 	0xA3
#define CMD_ERASE   	0xA4
#define CMD_PROGRAM 	0xA5
#define CMD_VERIFY  	0xA6
#define CMD_READ_CONFIG	0xA7
#define CMD_WRITE_CONFIG	0xA8
#define CMD_DATA_ERASE	0xA9
#define CMD_DATA_PROGRAM	0xAA
#define CMD_DATA_READ	0xAB
#define CMD_WRITE_OTP	0xC3
#define CMD_READ_OTP	0xC4
#define CMD_SET_BAUD	0xC5

typedef struct{
  uint8_t id;
  uint8_t type;
  uint8_t uid[8];
  uint16_t bootloader_ver;
  uint8_t xor_key[8];
  wch_if_t port;
  //info from database
  char *name;
}isp_dev;
typedef isp_dev* isp_dev_t;

//read id, type
static void isp_cmd_identify(isp_dev_t dev);
//read (uid | bootloader_ver | user option bytes) by cfgmask
static size_t isp_cmd_read_config(isp_dev_t dev, uint16_t cfgmask, size_t len, uint8_t data[]);
//
static void isp_cmd_write_config(isp_dev_t dev, uint16_t cfgmask, size_t len, uint8_t data[]);
//set ISP_KEY (source fot XOR_KEY)
static void isp_cmd_isp_key(isp_dev_t dev, size_t len, uint8_t *key, uint8_t *sum);
//exit (if 'reason' == 1 then reset)
static void isp_cmd_isp_end(isp_dev_t dev, uint8_t reason);
//
static char isp_cmd_erase(isp_dev_t dev, uint32_t sectors);
//
static uint16_t isp_cmd_program(isp_dev_t dev, uint32_t addr, uint16_t len, uint8_t data[]);
//
static uint16_t isp_cmd_verify(isp_dev_t dev, uint32_t addr, uint16_t len, uint8_t data[]);


////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////
//     ISP commands
////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////
static void isp_cmd_identify(isp_dev_t dev){
  if(dev->port == NULL){fprintf(stderr, "isp_cmd_identify: wch_if is NULL\n"); return;}
  uint8_t buf[] = "\0\0MCU ISP & WCH.CN";
  dev->port->send(dev->port, CMD_IDENTIFY, sizeof(buf)-1, buf);
  dev->port->recv(dev->port, CMD_IDENTIFY, 2, buf);
  dev->id = buf[0];
  dev->type = buf[1];
}

static size_t isp_cmd_read_config(isp_dev_t dev, uint16_t cfgmask, size_t len, uint8_t data[]){
  uint8_t buf[60];
  uint16_t retmask;
  size_t res;
  
  buf[0] = (cfgmask >> 0) & 0xFF;
  buf[1] = (cfgmask >> 8) & 0xFF;
  
  dev->port->send(dev->port, CMD_READ_CONFIG, 2, buf);
  res = dev->port->recv(dev->port, CMD_READ_CONFIG, sizeof(buf), buf);
  
  if(res < 2){
    fprintf(stderr, "isp_cmd_read_config fail: not received enough bytes\n");
    return 0;
  }
  retmask = (uint16_t)buf[1]<<8 | buf[0];
  if(cfgmask != retmask){
    fprintf(stderr, "isp_cmd_read_config fail: received conf does not match\n");
    return 0;
  }
  if(res-2 < len)len = res-2;
  if(data)memcpy(data, &buf[2], len);
  return len;
}

static void isp_cmd_write_config(isp_dev_t dev, uint16_t cfgmask, size_t len, uint8_t data[]){
  uint8_t buf[64];
  buf[0] = (cfgmask >> 0) & 0xFF;
  buf[1] = (cfgmask >> 8) & 0xFF;
  if(len > dev->port->maxdatasize-3)len = dev->port->maxdatasize-3;
  memcpy(&buf[2], data, len);
  dev->port->send(dev->port, CMD_WRITE_CONFIG, len+2, buf);
  dev->port->recv(dev->port, CMD_WRITE_CONFIG, 2, buf);
}

static void isp_cmd_isp_key(isp_dev_t dev, size_t len, uint8_t *key, uint8_t *sum){
  uint8_t buf[2];
  uint8_t uid_sum = 0;
  if(len < 29){fprintf(stderr, "isp_cmd_isp_key: key must be at least 29 bytes\n"); return;}
  
  dev->port->send(dev->port, CMD_ISP_KEY, len, key);
  dev->port->recv(dev->port, CMD_ISP_KEY, 2, buf);
  if(sum)*sum = buf[0];
  
  for(int i=0; i<8; i++)uid_sum += dev->uid[i];
  
  //XOR key generation algorythm. At least fot bootloader ver 02.09
  dev->xor_key[0] = uid_sum ^ key[len / 7 * 4];
  dev->xor_key[1] = uid_sum ^ key[len / 5 * 1];
  dev->xor_key[2] = uid_sum ^ key[len / 7 * 1];
  dev->xor_key[3] = uid_sum ^ key[len / 7 * 6];
  dev->xor_key[4] = uid_sum ^ key[len / 7 * 3];
  dev->xor_key[5] = uid_sum ^ key[len / 5 * 3];
  dev->xor_key[6] = uid_sum ^ key[len / 7 * 5];
  dev->xor_key[7] = dev->id + dev->xor_key[0];
}

static void isp_cmd_isp_end(isp_dev_t dev, uint8_t reason){
  uint8_t buf[2];
  dev->port->send(dev->port, CMD_ISP_END, sizeof(reason), &reason);
  dev->port->recv(dev->port, CMD_ISP_END, sizeof(buf), buf);
}

static char isp_cmd_erase(isp_dev_t dev, uint32_t sectors){
  uint8_t buf[64];
  buf[0] = (sectors >>  0) & 0xff;
  buf[1] = (sectors >>  8) & 0xff;
  buf[2] = (sectors >> 16) & 0xff;
  buf[3] = (sectors >> 24) & 0xff;
  
  dev->port->send(dev->port, CMD_ERASE, 4, buf);
  dev->port->recv(dev->port, CMD_ERASE, 2, buf);
  
  if(buf[0] != 0 || buf[1] != 0){
    fprintf(stderr, "isp_cmd_erase: failed: %.2X %.2X\n", buf[0], buf[1]);
    return 0;
  }
  return 1;
}

static uint16_t isp_cmd_program(isp_dev_t dev, uint32_t addr, uint16_t len, uint8_t data[]){
  uint8_t buf[64];
  if(len > dev->port->maxdatasize-5){
    fprintf(stderr, "isp_cmd_program: data boot big, %d (max=%d)\n", len, dev->port->maxdatasize-5);
    return 0;
  }
  buf[0] = (addr >>  0) & 0xff;
  buf[1] = (addr >>  8) & 0xff;
  buf[2] = (addr >> 16) & 0xff;
  buf[3] = (addr >> 24) & 0xff;
  buf[4] = 0; //padding (ignored by bootloader)
  
  for(int i=0; i<len; i++)buf[i+5] = data[i] ^ dev->xor_key[i % 8];
  
  dev->port->send(dev->port, CMD_PROGRAM, len+5, buf);
  dev->port->recv(dev->port, CMD_PROGRAM, 2, buf);
  
  if(buf[0] != 0 || buf[1] != 0){
    fprintf(stderr, "isp_cmd_program: failed at address 0x%.8X\n", addr);
    return 0;
  }
  return len;
}

static uint16_t isp_cmd_verify(isp_dev_t dev, uint32_t addr, uint16_t len, uint8_t data[]){
  uint8_t buf[64];
  if(len > dev->port->maxdatasize-5){
    fprintf(stderr, "isp_cmd_verify: data boot big, %d (max=%d)\n", len, dev->port->maxdatasize-5);
    return 0;
  }
  buf[0] = (addr >>  0) & 0xff;
  buf[1] = (addr >>  8) & 0xff;
  buf[2] = (addr >> 16) & 0xff;
  buf[3] = (addr >> 24) & 0xff;
  buf[4] = 0; //padding (ignored by bootloader)
  
  for(int i=0; i<len; i++)buf[i+5] = data[i] ^ dev->xor_key[i % 8];
  
  dev->port->send(dev->port, CMD_VERIFY, len+5, buf);
  dev->port->recv(dev->port, CMD_VERIFY, 2, buf);
  
  if(buf[0] != 0 || buf[1] != 0){
    fprintf(stderr, "isp_cmd_verify: failed at address 0x%.8X\n", addr);
    return 0;
  }
  return len;
}

////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////
//   Read file
////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////

static void file_read_bin(const char name[], size_t *sz, uint8_t **data){
  int res;
  size_t len;
  uint8_t *buf = NULL;
  FILE *pf = fopen(name, "rb");
  if(pf == NULL){fprintf(stderr, "file_read_bin, open [%s]: %s\n", name, strerror(errno)); return;}
  res = fseek(pf, 0, SEEK_END);
  if(res == -1){fprintf(stderr, "file_read_bin, fseek: %s\n", strerror(errno)); return;}
  len = ftell(pf);
  res = fseek(pf, 0, SEEK_SET);
  if(res == -1){fprintf(stderr, "file_read_bin, fseek: %s\n", strerror(errno)); return;}
  buf = malloc(len);
  if(buf == NULL){fprintf(stderr, "file_read_bin, malloc %d bytes\n", (int)len); return;}
  size_t rdbytes = fread(buf, 1, len, pf);
  if(rdbytes != len){fprintf(stderr, "file_read_bin, read %d bytes (exp. %d)\n", (int)rdbytes, (int)len);}
  fclose(pf);
  *sz = rdbytes;
  *data = buf;
}

static void file_read_hex(const char name[], size_t *sz, uint8_t **data){
  size_t size = 0;
  uint8_t *buf = NULL;
  int res;
  size_t maxaddr = 0;
  int line = 0;
  uint8_t linesize;
  uint16_t lineaddr;
  uint16_t newaddr;
  uint8_t linecode;
  uint8_t linedata;
  uint8_t lrc;
  FILE *pf = fopen(name, "rt");
  if(pf == NULL){fprintf(stderr, "file_read_hex, open [%s]: %s\n", name, strerror(errno)); return;}
  while(1){
    line++;
    res = fscanf(pf, ":%2hhX%4hX%2hhX", &linesize, &lineaddr, &linecode);
    if(res != 3)break;
    if(linecode != 0)continue;
    lrc = linesize;
    lrc += (lineaddr >> 0) & 0xFF;
    lrc += (lineaddr >> 8) & 0xFF;
    newaddr = lineaddr + linesize;
    //realloc
    if(newaddr > size)size = newaddr;
    if(newaddr > maxaddr){
      size_t maxaddr_prev = maxaddr;
      uint8_t *buf_prev = buf;
      maxaddr = (newaddr + 1023) &~1023; //round up to 1k
      buf = realloc(buf_prev, maxaddr);
      if(buf == NULL){
        if(buf_prev)free(buf_prev); 
        *sz = 0;
        *data = NULL;
        return;
      }
      memset(&buf[maxaddr_prev], 0, (maxaddr - maxaddr_prev));
    }
    //read data
    for(int i=0; i<linesize; i++){
      res = fscanf(pf, "%2hhX", &linedata);
      if(res != 1){
        fprintf(stderr, "file_read_hex: file format error in line %i\n", line);
        if(buf)free(buf);
        *sz = 0; *data = NULL; fclose(pf);
        return;
      }
      buf[lineaddr + i] = linedata;
      lrc += linedata;
    }
    
    res = fscanf(pf, "%2hhX\n", &linedata);
    if(res != 1){
      fprintf(stderr, "file_read_hex: file format error in line %i\n", line);
      if(buf)free(buf);
      *sz = 0; *data = NULL; fclose(pf);
      return;
    }
    lrc += linedata;
    if(lrc != 0){
      fprintf(stderr, "file_read_hex: LRC error in line %i\n", line);
      if(buf)free(buf);
      *sz = 0; *data = NULL; fclose(pf);
      return;
    }
  }
  fclose(pf);
  *sz = size;
  *data = buf;
}

#ifdef _MSC_VER //not #if defined(_WIN32) || defined(_WIN64) because we have strncasecmp in mingw
  #define strcasecmp _stricmp
#endif

static void file_read(const char name[], size_t *sz, uint8_t **data){
  size_t len = strlen(name);
  if(len < 4){fprintf(stderr, "Input file must me *.hex or *.bin\n"); *sz=0; *data=NULL; return;}
  if(strcasecmp(&name[len-4], ".hex")==0){
    file_read_hex(name, sz, data);
  }else if(strcasecmp(&name[len-4], ".bin")==0){
    file_read_bin(name, sz, data);
  }else{
    fprintf(stderr, "Input file must me *.hex or *.bin\n");
    *sz=0; *data=NULL;
    return;
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////
//   High-level functions
////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////

static void dev_readinfo(isp_dev_t dev){
  uint8_t buf[12];
  size_t len;
  len = isp_cmd_read_config(dev, CFG_MASK_BTVER | CFG_MASK_UID, sizeof(buf), buf);
  if(len != 12){
    fprintf(stderr, "dev_readinfo fail: not received enough bytes\n");
    return;
  }
  dev->bootloader_ver = (uint16_t)buf[1]<<8 | buf[2];
  memcpy(dev->uid, &buf[4], 8);
}

static char do_verify(isp_dev_t dev, uint32_t addr, size_t size, uint8_t data[]){
  size_t count = 0;
  uint16_t res = 0xFFFF;
  uint16_t maxdata = dev->port->maxdatasize - 5;
  while(size){
    count = size;
    if(count > maxdata)count = maxdata;
    res = isp_cmd_verify(dev, addr, count, &data[addr]);
    if(res != count)break;
    addr += count;
    size -= count;
    //TODO: progressbar
  }
  free(data);
  if(res == count){printf("Verify: OK\n"); return 0;}else return 2;
}

static char do_program(isp_dev_t dev, uint32_t addr, size_t size, uint8_t data[]){
  size_t count = 0;
  uint16_t res = 0xFFFF;
  uint16_t maxdata = dev->port->maxdatasize - 5;
  while(size){
    count = size;
    if(count > maxdata)count = maxdata;
    res = isp_cmd_program(dev, addr, count, &data[addr]);
    if(res != count)break;
    addr += count;
    size -= count;
    //TODO: progressbar
  }
  free(data);
  if(res == count){printf("Program: OK\n"); return 0;}else return 2;
}

static char do_erase(isp_dev_t dev, size_t size){
  size = (size + 1023) / 1024;
  if(size < 8)size = 8;
  return isp_cmd_erase(dev, size);
}


#define PIN_NONE	0
#define PIN_RTS		1
#define PIN_DTR		2
#define PIN_nRTS	3
#define PIN_nDTR	4

struct{
  int pin_rst:3;
  int pin_boot:3;
  int noverify:1;
  int progress:1;
}run_flags = {
  .pin_rst = PIN_NONE,
  .pin_boot= PIN_NONE,
  .noverify = 0,
  .progress = 0,
};

void pin_rst_ctl(wch_if_t self, char state){
  switch(run_flags.pin_rst){
    case PIN_RTS: wch_if_uart_rts(self, state); break;
    case PIN_DTR: wch_if_uart_dtr(self, state); break;
    case PIN_nRTS:wch_if_uart_rts(self, !state); break;
    case PIN_nDTR:wch_if_uart_dtr(self, !state); break;
  }
}

void pin_boot_ctl(wch_if_t self, char state){
  switch(run_flags.pin_boot){
    case PIN_RTS: wch_if_uart_rts(self, state); break;
    case PIN_DTR: wch_if_uart_dtr(self, state); break;
    case PIN_nRTS:wch_if_uart_rts(self, !state); break;
    case PIN_nDTR:wch_if_uart_dtr(self, !state); break;
  }
}

static void rtsdtr_release(wch_if_t self){
  if(self->close != wch_if_close_uart)return;
  pin_boot_ctl(self, 0);
  pin_rst_ctl(self, 0);
  usleep(100000);
  pin_rst_ctl(self, 1);
}

static char match_rtsdtr(wch_if_t self){
  if(self->close != wch_if_close_uart)return 1;
  pin_boot_ctl(self, 1);
  pin_rst_ctl(self, 0);
  usleep(100000);
  pin_rst_ctl(self, 1);
  usleep(100000);
  pin_boot_ctl(self, 0);
  return 1;
}

char *dev_uid = NULL;
static char match_dev_list(wch_if_t self){
  isp_dev dev = {.id = 0, .type = 0};
  dev.port = self;
  match_rtsdtr(self);
  isp_cmd_identify(&dev);
  if(dev.id == 0 || dev.type == 0)return 0;
  dev_readinfo(&dev);
  printf("found bt ver %.4X uid = [", dev.bootloader_ver);
  for(int i=0; i<7; i++)printf("%.2X-", dev.uid[i]);
  printf("%.2X]\n", dev.uid[7]);
  return 0;
}
static char match_dev_uid(wch_if_t self){
  static uint8_t exp_uid[8] = {0};
  if(self == NULL){
    sscanf(dev_uid, "%2hhX-%2hhX-%2hhX-%2hhX-%2hhX-%2hhX-%2hhX-%2hhX",
           &exp_uid[0], &exp_uid[1], &exp_uid[2], &exp_uid[3],
           &exp_uid[4], &exp_uid[5], &exp_uid[6], &exp_uid[7]);
    return 0;
  }
  isp_dev dev = {.id = 0, .type = 0};
  dev.port = self;
  match_rtsdtr(self);
  isp_cmd_identify(&dev);
  if(dev.id == 0 || dev.type == 0)return 0;
  dev_readinfo(&dev);
  
  return (memcmp(exp_uid, dev.uid, 8) == 0);
}

void debug(wch_if_t interface, char comment[], uint16_t len, uint8_t buf[]){
  printf("~debug %s [%d]: ", comment, len);
  for(int i=0; i<len; i++)printf("%.2X ", buf[i]);
  printf("\n");
}

void show_version(char name[]){
  printf("%s %s\n", name, VERSION);
}

int rtsdtr_decode(char *str){
  if(strcmp(str, "RTS")==0){
    return PIN_RTS;
  }else if(strcmp(str, "DTR")==0){
    return PIN_DTR;
  }else if(strcmp(str, "nRTS")==0){
    return PIN_nRTS;
  }else if(strcmp(str, "nDTR")==0){
    return PIN_nDTR;
  }else{
    fprintf(stderr, "Error --reset / --boot0 option: wrong argument; Use default\n");
    return PIN_NONE;
  }
}

typedef char (*command_t)(isp_dev_t);
char *port_name = "usb";
command_t command = NULL;
char *filename = NULL;
wch_if_debug debug_func = NULL;
wch_if_match match_func = match_rtsdtr;


char command_write(isp_dev_t dev){
  printf("Write [%s]\n", filename);
  return 0;
}
char command_verify(isp_dev_t dev){
  printf("Verify [%s]\n", filename);
  return 0;
}
char command_erase(isp_dev_t dev){
  printf("Erase\n");
  return 0;
}
char command_unlock(isp_dev_t dev){
  printf("Unlock\n");
  return 0;
}
char command_info(isp_dev_t dev){
  printf("Info\n");
  return 0;
}


void help(char name[]){
  printf("usage: %s [OPTIONS] COMMAND [ARG]\n", name);
  printf("  OPTIONS:\n");
  printf("\t-v           Print version and exit\n");
  printf("\t-h, --help   Show this help and exit\n");
  printf("\t-n           No verify after writing\n");
  printf("\t-p           Show progress bar\n");
  printf("\t-d           Debug mode, show raw commands\n");
  printf("\t--port=USB   Specify port as USB (default)\n");
  printf("\t--port=/dev/ttyUSB0 \t Specify port as COM-port '/dev/ttyUSB0'\n");
  printf("\t--port='//./COM3'   \t Specify port as COM-port '//./COM3'\n");
  printf("\t--uid=AA-BB-CC-DD-EE-FF-GG-HH \tSpecify device UID\n");
  printf("\t--reset=PIN  Use PIN as RESET\n");
  printf("\t--boot0=PIN  Use PIN as Boot0\n");
  printf("\t    'PIN' may be 'RTS', 'DTR', 'nRTS' or 'nDTR'\n");
  
  
  printf("\n");
  printf("  COMMAND:\n");
  printf("\twrite FILE    write file (.hex or .bin)\n");
  printf("\tverify FILE   verify file (.hex ot .bin)\n");
  printf("\terase         erase all memory\n");
  printf("\tlist          show connected devices\n");
  printf("\tunlock        remove write protection\n");
  printf("\tinfo          show device info: bootloader version, option bytes, flash size etc\n");
}

#define StrEq(str, sample) (strncmp(str, sample, sizeof(sample)-1) == 0)
int main(int argc, char **argv){
  for(int i=1; i<argc; i++){
    if(argv[i][0] == '-' && argv[i][1] != '-'){
      for(int j=1; argv[i][j]!=0; j++){
        switch(argv[i][j]){
          case 'h': help(argv[0]); return 0;
          case 'v': show_version(argv[0]); return 0;
          case 'n': run_flags.noverify = 1; break;
          case 'p': run_flags.progress = 1; break;
          case 'd': debug_func = debug; break;
          default: printf("Unknown option [-%c]\n", argv[i][j]); return -1;
        }
      }
    }else if(StrEq(argv[i], "--help")){
      help(argv[0]); return 0;
    }else if(StrEq(argv[i], "--port=")){
      port_name = &argv[i][7];
    }else if(StrEq(argv[i], "--uid=")){
      dev_uid = &argv[i][6];
      match_dev_uid(NULL);
      match_func = match_dev_uid;
    }else if(StrEq(argv[i], "--reset=")){
      run_flags.pin_rst = rtsdtr_decode( &argv[i][8] );
    }else if(StrEq(argv[i], "--boot0=")){
      run_flags.pin_boot = rtsdtr_decode( &argv[i][8] );
    }else if(StrEq(argv[i], "write")){
      command = command_write;
      filename = argv[i+1];
      i++;
    }else if(StrEq(argv[i], "verify")){
      command = command_verify;
      filename = argv[i+1];
      i++;
    }else if(StrEq(argv[i], "erase")){
      command = command_erase;
    }else if(StrEq(argv[i], "list")){
      match_func = match_dev_list;
    }else if(StrEq(argv[i], "unlock")){
      command = command_unlock;
    }else if(StrEq(argv[i], "info")){
      command = command_info;
    }else{
      fprintf(stderr, "Unknown command [%s]\n", argv[i]); return -1;
    }
  }
  
  isp_dev dev;
  if(strcasecmp(port_name, "usb")==0){
    dev.port = wch_if_open_usb(match_func, debug_func);
  }else{
    if(run_flags.pin_rst != PIN_NONE)
    dev.port = wch_if_open_uart(port_name, match_func, debug_func);
  }
  if(match_func == match_dev_list)return 0;
  if(dev.port == NULL){
    fprintf(stderr, "Couldn't open port [%s]\n", port_name);
    return -2;
  }
  
  //debug
  isp_cmd_identify(&dev);
  dev_readinfo(&dev);
  printf("id = %.2X %.2X\n", dev.id, dev.type);
  printf("found bt ver %.4X uid = [", dev.bootloader_ver);
  for(int i=0; i<7; i++)printf("%.2X ", dev.uid[i]);
  printf("%.2X]\n", dev.uid[7]);
  
  
  
  rtsdtr_release(dev.port);
  wch_if_close(&dev.port);
}

int main1(){
  isp_dev dev;
  //dev.port = wch_if_open_usb( dev_match );
  dev.port = wch_if_open_uart("/dev/ttyUSB0", NULL, NULL);
  if(dev.port == NULL){
    fprintf(stderr, "Error opening port / device not found\n");
    return -1;
  }
  //wch_if_set_debug(dev.port, dbg);
  
  isp_cmd_identify(&dev);
  dev_readinfo(&dev);
  
  printf("id = %.2X %.2X\n", dev.id, dev.type);
  printf("found bt ver %.4X uid = [", dev.bootloader_ver);
  for(int i=0; i<7; i++)printf("%.2X ", dev.uid[i]);
  printf("%.2X]\n", dev.uid[7]);
  
  uint8_t isp_key[30] = {0};
  isp_cmd_isp_key(&dev, sizeof(isp_key), isp_key, NULL);
  
  
  //program(&dev, 1024, "test/blink.bin");
  //verify(&dev, 1024, "test/blink.bin");
  
  wch_if_close(&dev.port);
}