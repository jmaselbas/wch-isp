#include <stdio.h>
#include <string.h>
#include "wch_if.h"

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

static void isp_cmd_identify(isp_dev_t dev);
static size_t isp_cmd_read_config(isp_dev_t dev, uint16_t cfgmask, size_t len, uint8_t data[]);
static void dev_readinfo(isp_dev_t dev);



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

static char dev_match(wch_if_t self){
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
  dev.port = wch_if_open_usb( dev_match );
  if(dev.port == NULL){
    fprintf(stderr, "Error opening port\n");
    return -1;
  }
  wch_if_set_debug(dev.port, dbg);
  isp_cmd_identify(&dev);

  
  
  if(dev.port)dev.port->close(&dev.port);
}