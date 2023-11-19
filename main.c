#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
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
static void isp_cmd_erase(isp_dev_t dev, uint32_t sectors);
//
static uint16_t isp_cmd_program(isp_dev_t dev, uint32_t addr, uint16_t len, uint8_t data[]);
//
static uint16_t isp_cmd_verify(isp_dev_t dev, uint32_t addr, uint16_t len, uint8_t data[]);



static void dev_readinfo(isp_dev_t dev); //read id, type, bootloader_ver and uid



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
#warning TODO
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

static void isp_cmd_erase(isp_dev_t dev, uint32_t sectors){
  #warning TODO
}

static uint16_t isp_cmd_program(isp_dev_t dev, uint32_t addr, uint16_t len, uint8_t data[]){
  #warning TODO
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

static char verify(isp_dev_t dev, const char name[]){
  uint8_t *data = NULL;
  size_t data_size;
  uint32_t addr = 0;
  size_t count = 0;
  uint16_t res = 0xFFFF;
  uint16_t maxdata = dev->port->maxdatasize - 5;
  file_read_bin(name, &data_size, &data);
  if(data == NULL)return 1;
  while(data_size){
    count = data_size;
    if(count > maxdata)count = maxdata;
    res = isp_cmd_verify(dev, addr, count, &data[addr]);
    if(res != count)break;
    addr += count;
    data_size -= count;
    //TODO: progressbar
  }
  free(data);
  if(res == count){printf("Verify: OK\n"); return 0;}else return 2;
}


__attribute__((unused))static char dev_match(wch_if_t self){
  isp_dev dev = {.id = 0, .type = 0};
  dev.port = self;
  isp_cmd_identify(&dev);
  if(dev.id == 0 || dev.type == 0)return 0;
  dev_readinfo(&dev);
  printf("found bt ver %.4X uid = [", dev.bootloader_ver);
  for(int i=0; i<7; i++)printf("%.2X ", dev.uid[i]);
  printf("%.2X]\n", dev.uid[7]);
  return 1;
}

void dbg(wch_if_t interface, char comment[], uint16_t len, uint8_t buf[]){
  printf("~debug %s [%d]: ", comment, len);
  for(int i=0; i<len; i++)printf("%.2X ", buf[i]);
  printf("\n");
}

int main(){
  isp_dev dev;
  //dev.port = wch_if_open_usb( dev_match );
  dev.port = wch_if_open_uart("/dev/ttyUSB0", NULL);
  if(dev.port == NULL){
    fprintf(stderr, "Error opening port / device not found\n");
    return -1;
  }
  wch_if_set_debug(dev.port, dbg);
  
  isp_cmd_identify(&dev);
  dev_readinfo(&dev);
  
  printf("id = %.2X %.2X\n", dev.id, dev.type);
  printf("found bt ver %.4X uid = [", dev.bootloader_ver);
  for(int i=0; i<7; i++)printf("%.2X ", dev.uid[i]);
  printf("%.2X]\n", dev.uid[7]);
  
  uint8_t isp_key[] = {0xFD, 0x6E, 0xBC, 0xE0, 0x77, 0x1C, 0xA0, 0xAC, 0xF1, 0x51, 0x54, 0xD5, 0xE1, 0x7F, 0x12, 0x04, 0x5F, 0x2C, 0xD8, 0x8C, 0x7C, 0xF6, 0x5D, 0x95, 0xD4, 0xD4, 0x4A, 0x79, 0x67, 0x9E, 0xA7};
  isp_cmd_isp_key(&dev, sizeof(isp_key), isp_key, NULL);
  
  /*uint8_t *fil = NULL;
  size_t fil_len;
  file_read_bin("test/blink.bin", &fil_len, &fil);
  if(fil != NULL){
    uint16_t res = isp_cmd_verify(&dev, 0, 32, fil);
    printf("verify: %d\n", res);
  
    free(fil);
  }else{
    printf("err\n");
  }*/
  verify(&dev, "test/blink.bin");
  
  if(dev.port)dev.port->close(&dev.port);
}