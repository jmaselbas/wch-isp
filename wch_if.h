#ifndef __WCH_INTERFACE_H__
#define __WCH_INTERFACE_H__
#include <inttypes.h>
#include <stddef.h>

struct wch_if;
typedef struct wch_if* wch_if_t;

typedef void (*wch_if_debug)(wch_if_t interface, char comment[], uint16_t len, uint8_t buf[]);

struct wch_if{
  uint16_t maxdatasize;
  size_t (*send)(wch_if_t interface, uint8_t cmd, uint16_t len, uint8_t data[]);
  size_t (*recv)(wch_if_t interface, uint8_t cmd, uint16_t len, uint8_t data[]);
  void (*close)(wch_if_t *interface);
  wch_if_debug debug;
  void *intern;
};

typedef char (*wch_if_match)(wch_if_t);

wch_if_t wch_if_open_usb( wch_if_match match_func );
wch_if_t wch_if_open_uart(char portname[], wch_if_match match_func);
inline void wch_if_set_debug(wch_if_t interface, wch_if_debug debug_func){interface->debug = debug_func;}
inline void wch_if_close( wch_if_t *interface ){if(*interface != NULL)(*interface)->close(interface);}

#endif