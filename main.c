#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include "wch_if.h"
#include "wch_yaml_parse.h"

#define DATABASE_PATH "./devices"

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
  uint32_t optionbytes[3];
  wch_if_t port;
  char *name;
}isp_dev;
typedef isp_dev* isp_dev_t;

//read id, type
static char isp_cmd_identify(isp_dev_t dev);
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
static size_t isp_cmd_bulk(isp_dev_t dev, uint8_t cmd, uint32_t addr, size_t len, uint8_t *data);


////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////
//     ISP commands
////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////
static char isp_cmd_identify(isp_dev_t dev){
  if(dev->port == NULL){fprintf(stderr, "isp_cmd_identify: wch_if is NULL\n"); return 0;}
  uint8_t buf[] = "\0\0MCU ISP & WCH.CN";
  dev->port->send(dev->port, CMD_IDENTIFY, sizeof(buf)-1, buf);
  char res = dev->port->recv(dev->port, CMD_IDENTIFY, 2, buf);
  dev->id = buf[0];
  dev->type = buf[1];
  return res;
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

static size_t isp_cmd_bulk(isp_dev_t dev, uint8_t cmd, uint32_t addr, size_t len, uint8_t data[]){
  uint8_t buf[64];
  if(len > dev->port->maxdatasize-5){
    len = dev->port->maxdatasize-5;
  }
  buf[0] = (addr >>  0) & 0xff;
  buf[1] = (addr >>  8) & 0xff;
  buf[2] = (addr >> 16) & 0xff;
  buf[3] = (addr >> 24) & 0xff;
  buf[4] = 0; //padding (ignored by bootloader)
  
  for(int i=0; i<len; i++)buf[i+5] = data[i] ^ dev->xor_key[i % 8];
  //TODO: test
  size_t align = (len + 7)&~7;
  if(len != align){
    for(int i=len; i<align; i++)buf[i+5] = dev->xor_key[i % 8];
    //printf("align %zu -> %zu\n", len, align);
    len = align;
  }
  
  
  dev->port->send(dev->port, cmd, len+5, buf);
  dev->port->recv(dev->port, cmd, 2, buf);
  
  if(buf[0] != 0 || buf[1] != 0){
    if(cmd == CMD_VERIFY)fprintf(stderr, "isp_cmd_verify");
      else if(cmd == CMD_PROGRAM)fprintf(stderr, "isp_cmd_program");
      else fprintf(stderr, "isp_cmd_unknown (%.2X)", cmd);
    fprintf(stderr, ": failed at address 0x%.8X\n", addr);
    return 0;
  }
  return len;
}

////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////
//   Read file
////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////

size_t flash_align(size_t x){
  //return (x + 63) &~ 63;
  return x;
}

static void file_read_bin(const char name[], size_t *sz, uint8_t **data){
  int res;
  size_t len, size;
  uint8_t *buf = NULL;
  FILE *pf = fopen(name, "rb");
  if(pf == NULL){fprintf(stderr, "file_read_bin, open [%s]: %s\n", name, strerror(errno)); return;}
  res = fseek(pf, 0, SEEK_END);
  if(res == -1){fprintf(stderr, "file_read_bin, fseek: %s\n", strerror(errno)); return;}
  len = ftell(pf);
  res = fseek(pf, 0, SEEK_SET);
  if(res == -1){fprintf(stderr, "file_read_bin, fseek: %s\n", strerror(errno)); return;}
  
  size = flash_align(len);
  
  buf = malloc(len);
  if(buf == NULL){fprintf(stderr, "file_read_bin, malloc %d bytes\n", (int)len); return;}
  size_t rdbytes = fread(buf, 1, len, pf);
  if(rdbytes != len){
    fprintf(stderr, "file_read_bin, read %d bytes (exp. %d)\n", (int)rdbytes, (int)len);
  }
  fclose(pf);
  memset(&buf[rdbytes], 0, (size-rdbytes));
  *sz = size;
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
  size = flash_align(size);
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

static char dev_readinfo(isp_dev_t dev){
  uint8_t buf[12];
  size_t len;
  len = isp_cmd_read_config(dev, CFG_MASK_BTVER | CFG_MASK_UID, sizeof(buf), buf);
  if(len != 12){
    fprintf(stderr, "dev_readinfo fail: not received enough bytes\n");
    return 0;
  }
  dev->bootloader_ver = (uint16_t)buf[1]<<8 | buf[2];
  memcpy(dev->uid, &buf[4], 8);
  return 1;
}

static char dev_read_options(isp_dev_t dev){
  uint8_t buf[12];
  size_t len;
  len = isp_cmd_read_config(dev, CFG_MASK_RDPR_USER_DATA_WPR, sizeof(buf), buf);
  if(len != 12){fprintf(stderr, "Error reading info: not received enough bytes\n"); return 0;}
  //printf("DEBUG optionbytes: "); for(int i=0; i<12; i++)printf("%.2X ", buf[i]); printf("\n");
  
  dev->optionbytes[0] = ((uint32_t)buf[0] << 0 ) | ((uint32_t)buf[1] << 8 ) |
                        ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
            
  dev->optionbytes[1] = ((uint32_t)buf[4] << 0 ) | ((uint32_t)buf[5] << 8 ) |
                        ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
            
  dev->optionbytes[2] = ((uint32_t)buf[8] << 0 ) | ((uint32_t)buf[9] << 8 ) |
                        ((uint32_t)buf[10]<< 16) | ((uint32_t)buf[11]<< 24);
  return 1;
}

void progressbar(char comment[], float per){
  printf("\r%s%2.1f %%   ", comment, per);
  fflush(stdout);
}

static char do_erase(isp_dev_t dev, size_t size){
  size = (size + 1023) / 1024;
  //if(size < 8)size = 8;
  printf("Erase %zu sectors (%zu bytes)\n", size, size*1024);
  return isp_cmd_erase(dev, size);
}


#define PIN_NONE	0
#define PIN_RTS		1
#define PIN_DTR		2
#define PIN_nRTS	3
#define PIN_nDTR	4

#define COMMAND_ERR	15
#define COMMAND_NONE	0
#define COMMAND_WRITE	1
#define COMMAND_VERIFY	2
#define COMMAND_ERASE	3
#define COMMAND_UNLOCK	4
#define COMMAND_INFO	5
#define COMMAND_OPTION	6
#define COMMAND_OPTSHOW	7

struct{
  unsigned int pin_rst:3;
  unsigned int pin_boot:3;
  unsigned int cmd:4;
  unsigned int noverify:1;
  unsigned int progress:1;
  unsigned int fin_reset:1;
  unsigned int db_ignore:1;
  unsigned int ignore_flash_size:1;
  unsigned int ignore_flash_total:1;
}run_flags = {
  .pin_rst = PIN_NONE,
  .pin_boot= PIN_NONE,
  .cmd = COMMAND_NONE,
  .noverify = 0,
  .progress = 0,
  .fin_reset = 0,
  .db_ignore = 0,
  .ignore_flash_size = 0,
  .ignore_flash_total = 0,
};

static char do_bulk(isp_dev_t dev, uint8_t cmd, uint32_t addr, size_t size, uint8_t data[]){
  char *name;
  ssize_t sz = size;
  size_t count = 0;
  size_t res;
  
  if(cmd == CMD_VERIFY)name = "Verify: ";
    else if(cmd == CMD_PROGRAM)name = "Write: ";
    else{fprintf(stderr, "do_bulk.cmd must be either CMD_PROGRAM or CMD_VERIFY\n"); return 0;}
    
  while(sz>0){
    count = sz;
    res = isp_cmd_bulk(dev, cmd, addr, count, data);
    if(res == 0)return 0;
    addr += res;
    data += res;
    sz -= res;
    if(run_flags.progress)progressbar(name, 100.0 - 100.0*sz/size);
  }
  if(cmd == CMD_PROGRAM)isp_cmd_bulk(dev, cmd, addr, 0, NULL);
  return 1;
}

void command_info(isp_dev_t dev, wch_info_t *info){
  printf("Device %.2X %.2X, UID=", dev->id, dev->type);
  for(int i=0; i<7; i++)printf("%.2X-", dev->uid[i]);
  printf("%.2X\n", dev->uid[7]);
  printf("Bootloader v.%.4X\n", dev->bootloader_ver);
  if(!dev_read_options(dev))return;
  if(info){
    wch_info_regs_import(info, dev->optionbytes, sizeof(dev->optionbytes)/sizeof(dev->optionbytes[0]));
    wch_info_show(info);
  }
}

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
  char *dev_name = NULL;
  dev.port = self;
  match_rtsdtr(self);
  isp_cmd_identify(&dev);
  if(dev.id == 0 || dev.type == 0)return 0;
  dev_readinfo(&dev);
  
  wch_info_t *info = NULL;
  if(!run_flags.db_ignore){
    info = wch_info_read_dir(DATABASE_PATH, 1, dev.type, dev.id);
    if(info == NULL){
      fprintf(stderr, "Could not find the device [0x%.2X 0x%.2X] in databse\n", dev.type ,dev.id);
    }
    if(info->name)dev_name = info->name;
  }
  
  printf("found 0x%.2X 0x%.2X", dev.type, dev.id);
  if(dev_name)printf(" ( %s )", dev_name);
  printf(", bt ver.%.4X uid = [ ", dev.bootloader_ver);
  
  for(int i=0; i<7; i++)printf("%.2X-", dev.uid[i]);
  printf("%.2X ]\n", dev.uid[7]);
  wch_info_free(&info);
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

char* dbg_decode_cmd(uint8_t cmd){
  static char* cmd_A[] = {"IDENTIFY", "ISP_END", "ISP_KEY", "ERASE", "PROGRAM", "VERIFY", "READ_CONFIG", "WRITE_CONFIG", "DATA_ERASE", "DATA_PROGRAM", "DATA_READ"};
  static char* cmd_C[] = {"WRITE_OTP", "READ_OTP", "SET_BAUD"};
  static char* cmd_err = "Unknown (0x00)";
  if( (cmd >= 0xA1)&&(cmd <= 0xAB) )return cmd_A[cmd - 0xA1];
  if( (cmd >= 0xC3)&&(cmd <= 0xC5) )return cmd_C[cmd - 0xC3];
  sprintf(cmd_err, "Unknown (0x%.2X)", cmd);
  return cmd_err;
}

void debug(wch_if_t interface, char comment[], char dir_send, uint16_t len, uint8_t buf[]){
  uint8_t *data = &buf[interface->dbg_content_start];
  uint16_t dsize;
  if(dir_send){
    dsize = data[1] | (uint16_t)data[2]<<8;
  }else{
    dsize = data[2] | (uint16_t)data[3]<<8;
  }
  printf("%s cmd %s (%.2x) len %.4x : ", comment, dbg_decode_cmd(data[0]), data[0], dsize);
  dsize = len - interface->dbg_content_start - 4 + dir_send;
  for(int i=0; i<dsize; i++)printf("%.2x", data[i+4-dir_send]);
  printf("\n");
}

void debug1(wch_if_t interface, char comment[], uint16_t len, uint8_t buf[]){
  printf("~debug %s [%d]: ", comment, len);
  for(int i=0; i<len; i++)printf("%.2X ", buf[i]);
  printf("\n");
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
char *dev_name = NULL;
char *filename = NULL;
char *optionstr = NULL;
const char optstr_unlock_RDPR[] = "RDPR=0xA5";
wch_if_debug debug_func = NULL;
wch_if_match match_func = match_rtsdtr;
uint32_t writeaddr = 0;


void show_version(char name[]){
  printf("%s %s\n", name, VERSION);
}

#ifndef BUILD_SMALL
void help(char name[]){
  printf("usage: %s [OPTIONS] COMMAND [ARG]\n", name);
  printf("  OPTIONS:\n");
  printf("\t-v           Print version and exit\n");
  printf("\t-h, --help   Show this help and exit\n");
  printf("\t-n           No verify after writing\n");
  printf("\t-p           Show progress bar\n");
  printf("\t-d           Debug mode, show raw commands\n");
  printf("\t-r           Reset after command completed\n");
  printf("\t-b           Do not read database\n");
  printf("\t-f           Ignore if firmware size more than cached flash size (program memory)\n");
  printf("\t-F           Ignore if firmware size more than total flash size (program memory + const data memory)\n");
  printf("\t--port=USB   Specify port as USB (default)\n");
  printf("\t--port=/dev/ttyUSB0 \t Specify port as COM-port '/dev/ttyUSB0'\n");
  printf("\t--port='//./COM3'   \t Specify port as COM-port '//./COM3'\n");
  printf("\t--device=DEV Test if connected device is DEV and exit if they differ\n");
  printf("\t--uid=AA-BB-CC-DD-EE-FF-GG-HH \tSpecify device UID\n");
  printf("\t--reset=PIN  Use PIN as RESET\n");
  printf("\t--boot0=PIN  Use PIN as Boot0\n");
  printf("\t    'PIN' may be 'RTS', 'DTR', 'nRTS' or 'nDTR'\n");
  printf("\t--address=0x08000000\t Write or verify data from specified address\n");
  
  
  printf("\n");
  printf("  COMMAND:\n");
  printf("\twrite FILE        write file (.hex or .bin)\n");
  printf("\tverify FILE       verify file (.hex ot .bin)\n");
  printf("\terase             erase all memory\n");
  printf("\tlist              show connected devices\n");
  printf("\tunlock            remove write protection\n");
  printf("\tinfo              show device info: bootloader version, option bytes, flash size etc\n");
  printf("\toptionbytes 'CMD' change optionbytes\n");
  printf("\t    example: %s optionbytes 'RDPR=0xA5, DATA0 = 0x42'\n", name);
  printf("\toptionshow 'CMD'  show changes after apply CMD to optionbytes; Do not write\n");
}
#else
void help(char name[]){
  printf("usage: %s --port=<PORT> [OPTIONS] COMMAND [ARG]\n", name);
  printf("  OPTIONS:\n");
  printf("\t-v           Print version and exit\n");
  printf("\t-h, --help   Show this help and exit\n");
  printf("\t--port=/dev/ttyUSB0 \t Specify port as COM-port '/dev/ttyUSB0'\n");
  printf("\t--port='//./COM3'   \t Specify port as COM-port '//./COM3'\n");
  printf("\t--reset=PIN  Use PIN as RESET\n");
  printf("\t--boot0=PIN  Use PIN as Boot0\n");
  printf("\t    'PIN' may be 'RTS', 'DTR', 'nRTS' or 'nDTR'\n");

  printf("\n");
  printf("  COMMAND:\n");
  printf("\twrite FILE        write file (.hex or .bin)\n");
  printf("\tverify FILE       verify file (.hex ot .bin)\n");
}

void init_small(){
  run_flags.db_ignore = 1;
}
#endif

#define StrEq(str, sample) (strncmp(str, sample, sizeof(sample)-1) == 0)
int main(int argc, char **argv){
  if(argc < 2){ help(argv[0]); return 0; }
  for(int i=1; i<argc; i++){
    if(argv[i][0] == '-' && argv[i][1] != '-'){
      for(int j=1; argv[i][j]!=0; j++){
        switch(argv[i][j]){
          case 'h': help(argv[0]); return 0;
          case 'v': show_version(argv[0]); return 0;
          case 'n': run_flags.noverify = 1; break;
          case 'p': run_flags.progress = 1; break;
          case 'd': debug_func = debug; break;
          case 'r': run_flags.fin_reset = 1; break;
          case 'b': run_flags.db_ignore = 1; break;
          case 'f': run_flags.ignore_flash_size=1; break;
          case 'F': run_flags.ignore_flash_total=1; break;
          default: printf("Unknown option [-%c]\n", argv[i][j]); return -1;
        }
      }
    }else if(StrEq(argv[i], "--help")){
      help(argv[0]); return 0;
    }else if(StrEq(argv[i], "--port=")){
      port_name = &argv[i][7];
    }else if(StrEq(argv[i], "--device=")){
      dev_name = &argv[i][9];
    }else if(StrEq(argv[i], "--uid=")){
      dev_uid = &argv[i][6];
      match_dev_uid(NULL);
      match_func = match_dev_uid;
    }else if(StrEq(argv[i], "--reset=")){
      run_flags.pin_rst = rtsdtr_decode( &argv[i][8] );
    }else if(StrEq(argv[i], "--boot0=")){
      run_flags.pin_boot = rtsdtr_decode( &argv[i][8] );
    }else if(StrEq(argv[i], "--address=")){
      sscanf(&argv[i][10], "%X", &writeaddr);
    }else
    //// COMMANDS //////////////////////
    if(StrEq(argv[i], "write")){
      run_flags.cmd = COMMAND_WRITE;
      filename = argv[i+1];
      i++;
    }else if(StrEq(argv[i], "verify")){
      run_flags.cmd = COMMAND_VERIFY;
      filename = argv[i+1];
      i++;
    }else if(StrEq(argv[i], "erase")){
      run_flags.cmd = COMMAND_ERASE;
    }else if(StrEq(argv[i], "list")){
      match_func = match_dev_list;
    }else if(StrEq(argv[i], "unlock")){
      run_flags.cmd = COMMAND_UNLOCK;
    }else if(StrEq(argv[i], "info")){
      run_flags.cmd = COMMAND_INFO;
    }else if(StrEq(argv[i], "optionbytes")){
      run_flags.cmd = COMMAND_OPTION;
      optionstr = argv[i+1];
      i++;
    }else if(StrEq(argv[i], "optionshow")){
      run_flags.cmd = COMMAND_OPTSHOW;
      optionstr = argv[i+1];
      i++;
    }else{
      fprintf(stderr, "Unknown command [%s]\n", argv[i]); return -1;
    }
  }

#ifdef BUILD_SMALL
  init_small();
#endif
  
  //Connect
  isp_dev dev;
  if(strcasecmp(port_name, "usb")==0){
    dev.port = wch_if_open_usb(match_func, debug_func);
  }else{
    dev.port = wch_if_open_uart(port_name, match_func, debug_func);
  }
  if(match_func == match_dev_list)return 0;
  if(dev.port == NULL){
    fprintf(stderr, "Couldn't open port [%s]\n", port_name);
    return -2;
  }
  
  //run command
  if( !isp_cmd_identify(&dev) ){
    run_flags.cmd = COMMAND_ERR;
  }else if( !dev_readinfo(&dev) ){
    run_flags.cmd = COMMAND_ERR;
  }
  
  wch_info_t *info = NULL;
  if(!run_flags.db_ignore && run_flags.cmd != COMMAND_ERR){
    info = wch_info_read_dir(DATABASE_PATH, 1, dev.type, dev.id);
    if(info == NULL){
      fprintf(stderr, "Could not find the device [0x%.2X 0x%.2X] in databse\n", dev.type ,dev.id);
      run_flags.cmd = COMMAND_ERR;
    }
    if(dev_name && strcasecmp(dev_name, info->name)!=0){
      printf("Connected device is [ %s ] instead of [ %s ]\n", info->name, dev_name);
      run_flags.cmd = COMMAND_ERR;
    }
  }
  
  
  if(run_flags.cmd == COMMAND_WRITE || run_flags.cmd == COMMAND_VERIFY){
    uint8_t *data = NULL;
    size_t size = 0;
    char res = 0;
    
    if(info != NULL){
      wch_regs_t *reg = NULL;
      wch_bitfield_t *rdpr = NULL;
      char res = dev_read_options(&dev);
      if(!res){fprintf(stderr, "Reading option bytes from MCU: failed\n");}
      wch_info_regs_import(info, dev.optionbytes, sizeof(dev.optionbytes)/sizeof(dev.optionbytes[0]));
      reg = wch_bitfield_byname(info, "RDPR", &rdpr);
      if(reg!=NULL && rdpr != NULL){
        uint32_t val = wch_bitfield_val(rdpr, reg->curval);
        if(val != 0xA5)printf("\nWARNING: mcu locked by RDPR bits. Try to run 'unlock' command\n\n");
      }
    }
    
    file_read(filename, &size, &data);
    //printf("file size = %zu\n", size);
    if(!run_flags.db_ignore){
      if(info->errflag || info->flash_size == 0){
        fprintf(stderr, "Error reading databse\n");
        free(data);
        data = NULL;
        run_flags.cmd = COMMAND_ERR;
      }
      if(size > info->flash_size){
        if((info->flash_total == 0)||(size > info->flash_total)){
          fprintf(stderr, "Firmware size (%zu kB) more then avaible flash (%zu kB)\n", (size_t)(size+1023)/1024, (size_t)(info->flash_size + 1023)/1024);
          if(!run_flags.ignore_flash_size && !run_flags.ignore_flash_total){
            free(data);
            data = NULL;
            run_flags.cmd = COMMAND_ERR;
          }
        }else{
          fprintf(stderr, "WARNING: Firmware size (%zu kB) more then cached flash (%zu kB)\n", (size_t)(size+1023)/1024, (size_t)(info->flash_size + 1023)/1024);
          fprintf(stderr, "  use flag '-f' to ignore\n");
          if(!run_flags.ignore_flash_total){
            free(data);
            data = NULL;
            run_flags.cmd = COMMAND_ERR;
          }
        }
      }
    }
    
    if(data != NULL){
      uint8_t isp_key[30] = {0};
      isp_cmd_isp_key(&dev, sizeof(isp_key), isp_key, NULL);
      
      if(run_flags.cmd == COMMAND_WRITE){
        res = do_erase(&dev, size);
        if(!res){
          printf("Erase: FAIL\n");
          size = 0;
        }
        res = do_bulk(&dev, CMD_PROGRAM, writeaddr, size, data);
        if(!res){
          printf("Write: FAIL\n");
        }else{
          printf("Write %zu bytes: DONE\n", size);
          if(!run_flags.noverify)run_flags.cmd = COMMAND_VERIFY;
        }
      }
      
      if(run_flags.cmd == COMMAND_VERIFY){
        res = do_bulk(&dev, CMD_VERIFY, writeaddr, size, data);
        if(!res){
          printf("Verify: FAIL\n");
        }else{
          printf("Verify %zu bytes: DONE\n", size);
        }
      }
      free(data);
    }
  } //COMMAND_WRITE, COMMAND_VERIFY
  
  if(run_flags.cmd == COMMAND_ERASE){
    if(info == NULL || info->errflag || info->flash_size == 0){
      fprintf(stderr, "Reading database failed but it necessary for correct execution of the command\n");
    }else{
      char res = do_erase(&dev, info->flash_size);
      if(!res){
        printf("Erase: FAIL\n");
      }  
    }
  }
  
  if(run_flags.cmd == COMMAND_INFO)command_info(&dev, info);
  
  do{
    if(run_flags.cmd == COMMAND_OPTION || run_flags.cmd == COMMAND_OPTSHOW || run_flags.cmd == COMMAND_UNLOCK){
      if(info == NULL || info->errflag){
        fprintf(stderr, "Reading database failed but it necessary for correct execution of the command\n");
        break;
      }
      //printf("optionstr = [%s]\n", optionstr);
        
      char res = dev_read_options(&dev);
      if(!res){fprintf(stderr, "Reading option bytes from MCU: failed\n"); break;}
      wch_info_regs_import(info, dev.optionbytes, sizeof(dev.optionbytes)/sizeof(dev.optionbytes[0]));
      
      if(run_flags.cmd == COMMAND_UNLOCK){
        wch_regs_t *reg = NULL;
        wch_bitfield_t *rdpr = NULL;
        char res = dev_read_options(&dev);
        if(!res){fprintf(stderr, "Reading option bytes from MCU: failed\n"); break;}
        wch_info_regs_import(info, dev.optionbytes, sizeof(dev.optionbytes)/sizeof(dev.optionbytes[0]));
        reg = wch_bitfield_byname(info, "RDPR", &rdpr);
        if(reg!=NULL && rdpr != NULL){
          uint32_t val = wch_bitfield_val(rdpr, reg->curval);
          if(val == 0xA5){printf("Device is already unlocked; Do nothing\n"); break;}
          optionstr = (char*)optstr_unlock_RDPR;
        }else{
          printf("The unlocking method is unknown for this device; Do nothing\n");
          break;
        }
      }
      
      res = wch_info_modify(info, optionstr);
      if(!res){fprintf(stderr, "Can not apply command '%s' to optionbytes\n", optionstr); break;}
      
      if(run_flags.cmd == COMMAND_OPTSHOW){
        wch_info_show(info);
      }else{
        uint32_t regs[3];
        uint8_t data[12];
        wch_info_regs_export(info, regs, sizeof(regs)/sizeof(regs[0]));
        data[0] = (regs[0] >> 0) & 0xFF; data[1] = (regs[0] >> 8) & 0xFF;
        data[2] = (regs[0] >>16) & 0xFF; data[3] = (regs[0] >>24) & 0xFF;
        data[4] = (regs[1] >> 0) & 0xFF; data[5] = (regs[1] >> 8) & 0xFF;
        data[6] = (regs[1] >>16) & 0xFF; data[7] = (regs[1] >>24) & 0xFF;
        data[8] = (regs[2] >> 0) & 0xFF; data[9] = (regs[2] >> 8) & 0xFF;
        data[10]= (regs[2] >>16) & 0xFF; data[11]= (regs[2] >>24) & 0xFF;
        //printf("DEBUG result     : "); for(int i=0; i<12; i++)printf("%.2X ", data[i]); printf("\n");
        isp_cmd_write_config(&dev, CFG_MASK_RDPR_USER_DATA_WPR, sizeof(data), data);
        printf("Option bytes write:\n  0x%.8X\n  0x%.8X\n  0x%.8X\nDone\n", regs[0], regs[1], regs[2]);
      }
    }
  }while(0);
  
  if(run_flags.fin_reset)isp_cmd_isp_end(&dev, 1);
  
  wch_info_free(&info);
  
  rtsdtr_release(dev.port);
  wch_if_close(&dev.port);
}
