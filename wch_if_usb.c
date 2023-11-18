#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libusb.h>
#include "wch_if.h"

#define ISP_VID		0x4348
#define ISP_PID		0x55e0
#define ISP_EP_OUT	0x02
#define ISP_EP_IN	0x82

typedef struct{
  libusb_context *usb;
  libusb_device_handle *dev;
  libusb_device *cur;
  unsigned int kernel;
}usb_if_t;

static size_t usb_if_send(wch_if_t interface, uint8_t cmd, uint16_t len, uint8_t data[]);
static size_t usb_if_recv(wch_if_t interface, uint8_t cmd, uint16_t len, uint8_t data[]);
static void usb_if_close(wch_if_t *interface);
#define SELF ( (usb_if_t*)(interface->intern) )

static char wch_if_match_default(wch_if_t self){return 1;}


wch_if_t wch_if_open_usb( wch_if_match match_func ){
  if(match_func == NULL)match_func = wch_if_match_default;
  
  wch_if_t port = (wch_if_t)malloc(sizeof(struct wch_if));
  if(port == NULL)return NULL;
  usb_if_t *usb = (usb_if_t*)malloc(sizeof(usb_if_t));
  usb->usb = NULL;
  usb->dev = NULL;
  usb->kernel = 0;
  port->maxdatasize = 64-3;
  port->send = usb_if_send;
  port->recv = usb_if_recv;
  port->close = usb_if_close;
  port->debug = NULL;
  port->dev = NULL;
  port->intern = usb;
  
  int res;
  libusb_device **list;
  libusb_device *cur = NULL;
  struct libusb_device_descriptor desc;
  
  res = libusb_init(&usb->usb);
  if(res){fprintf(stderr, "Usb init: fail\n"); return NULL;}
  
  ssize_t dev_count = libusb_get_device_list(usb->usb, &list);
  if(dev_count < 0){fprintf(stderr, "Usb get device list: fail\n"); return NULL;}
  
  for(int i=0; i<dev_count; i++){
    usb->kernel = 0;
    res = libusb_get_device_descriptor(list[i], &desc);
    if(res < 0)continue;
    
    if(desc.idVendor != ISP_VID || desc.idProduct != ISP_PID)continue;
    
    cur = libusb_ref_device(list[i]);
    
    res = libusb_open(cur, &usb->dev);
    if(res < 0){
      fprintf(stderr, "libusb_open: %s\n", libusb_strerror(res));
      break;
    }
    
    res = libusb_kernel_driver_active(usb->dev, 0);
    if(res == LIBUSB_ERROR_NOT_SUPPORTED)res = 0;
    if(res < 0){fprintf(stderr, "libusb_kernel_driver_active: %s\n", libusb_strerror(res)); continue;}
    usb->kernel = res;
    if(usb->kernel == 1){
      if(libusb_detach_kernel_driver(usb->dev, 0)){
        fprintf(stderr, "Couldn't detach kernel driver!\n");
        break;
      }
    }
    res = libusb_claim_interface(usb->dev, 0);
    if(res){fprintf(stderr, "libusb_claim_interface: %s\n", libusb_strerror(res)); break;}
    
    res = match_func(port);
    if(res){
      libusb_free_device_list(list, 1);
      return port;
    }
    
    res = libusb_release_interface(usb->dev, 0);
    if(res){fprintf(stderr, "libusb_release_interface: %s\n", libusb_strerror(res));}
    if(usb->kernel == 1)libusb_attach_kernel_driver(usb->dev, 0);
    libusb_close(usb->dev);
    libusb_unref_device(cur);
    usb->dev = NULL;
  }
  libusb_free_device_list(list, 1);
  usb_if_close(&port);
  return NULL;
}

static size_t usb_if_send(wch_if_t interface, uint8_t cmd, uint16_t len, uint8_t data[]){
  uint8_t buf[64];
  int count, res;
  if((size_t)(len + 3) > sizeof(buf)){
    fprintf(stderr, "usb_if_send: invalid argument, length %d\n", len);
    return 0;
  }
  buf[0] = cmd;
  buf[1] = (len >> 0) & 0xFF;
  buf[2] = (len >> 8) & 0xFF;
  if(len > 0)memcpy(&buf[3], data, len);

  if( interface->debug )interface->debug(interface, "usb_if_send", len+3, buf);
  
  res = libusb_bulk_transfer( SELF->dev, ISP_EP_OUT, buf, len + 3, &count, 10000);
  if(res){fprintf(stderr, "usb_if_send: %s\n", libusb_strerror(res)); return 0;}
  return count;
}

static size_t usb_if_recv(wch_if_t interface, uint8_t cmd, uint16_t len, uint8_t data[]){
  uint8_t buf[64];
  int res, count;
  uint16_t datalen;
  if((size_t)(len + 4) > sizeof(buf)){
    fprintf(stderr, "usb_if_recv: invalid argument, length %d\n", len);
    return 0;
  }
  res = libusb_bulk_transfer(SELF->dev, ISP_EP_IN, buf, len + 4, &count, 10000);
  if(res){fprintf(stderr, "usb_if_send: %s\n", libusb_strerror(res)); return 0;}
  if(count < 4){fprintf(stderr, "usb_if_recv: not enough data recv\n"); return 0;}
  if(buf[0] != cmd){
    fprintf(stderr, "usb_if_recv: got wrong 'cmd' %.2X (exp %.2X)\n", buf[0], cmd);
    return 0;
  }
  if(buf[1]){fprintf(stderr, "usb_if_recv: cmd error %.2X\n", buf[1]); return 0;}
  count -= 4;
  datalen = buf[2] | ((uint16_t)buf[3] << 8);
  if(datalen != count){
    fprintf(stderr, "usb_if_recv: length mismatch, %d ('len'=%d)\n", count, datalen);
    return 0;
  }
  if(count < len)len = count;

  if(data != NULL)memcpy(data, buf + 4, len);

  if( interface->debug )interface->debug(interface, "usb_if_recv", count+4, buf);

  return count;
}

static void usb_if_close(wch_if_t *interface){
  usb_if_t *usb = (usb_if_t*)((*interface)->intern);
  int res = 0;
  libusb_device *dev = NULL;
  if(usb->dev)dev = libusb_get_device(usb->dev);
  //printf("dev = %p\n", dev);
  
  if(usb->dev)res = libusb_release_interface(usb->dev, 0);
  if(res){fprintf(stderr, "libusb_release_interface: %s\n", libusb_strerror(res));}
  //printf("usb_dev = %p\n", usb->dev);
  
  if(usb->kernel == 1)libusb_attach_kernel_driver(usb->dev, 0);
  
  if(usb->dev)libusb_close(usb->dev);
  //printf("close\n");
  
  if(dev)libusb_unref_device(dev);
  //printf("unref\n");
  
  if (usb)libusb_exit(usb->usb);
  
  free(usb);
  free(*interface);
  *interface = NULL;
}