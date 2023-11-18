#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wch_if.h"

struct wch_if_uart;
typedef struct wch_if_uart* wch_if_uart_t;

wch_if_uart_t wch_if_uart_open(char name[], unsigned int baudrate);
int wch_if_uart_close(wch_if_uart_t tty);
ssize_t wch_if_uart_write(wch_if_uart_t tty, void *buf, size_t count);
ssize_t wch_if_uart_read(wch_if_uart_t tty, void *buf, size_t count);
int wch_if_uart_timeout(wch_if_uart_t tty, ssize_t time_ms);
void wch_if_uart_rts(wch_if_uart_t tty, char state);
void wch_if_uart_dtr(wch_if_uart_t tty, char state);


static size_t uart_if_send(wch_if_t interface, uint8_t cmd, uint16_t len, uint8_t data[]);
static size_t uart_if_recv(wch_if_t interface, uint8_t cmd, uint16_t len, uint8_t data[]);
static void uart_if_close(wch_if_t *interface);
#define TTY ( (wch_if_uart_t)(interface->intern) )

wch_if_t wch_if_open_uart(char portname[], wch_if_match match_func){
  if(portname == NULL){fprintf(stderr, "portname = NULL\n"); return NULL;}
//TODO if portname == NULL add function to list all avaible COM-ports and try to connect
  
  wch_if_t port = (wch_if_t)malloc(sizeof(struct wch_if));
  if(port == NULL)return NULL;
  
  wch_if_uart_t tty = wch_if_uart_open(portname, 115200);
  wch_if_uart_timeout(tty, 1000);
  
  port->maxdatasize = 61;
  port->send = uart_if_send;
  port->recv = uart_if_recv;
  port->close = uart_if_close;
  port->debug = NULL;
  port->intern = tty;
  
  if(match_func){
    if(!match_func(port))uart_if_close(&port);
  }
  
  return port;
}

static size_t uart_if_send(wch_if_t interface, uint8_t cmd, uint16_t len, uint8_t data[]){
  uint8_t buf[67];
  if(len > (sizeof(buf)-6)){
    fprintf(stderr, "uart_if_send: invalid argument, length %d\n", len);
    return 0;
  }
  buf[0] = 0x57;
  buf[1] = 0xAB;
  buf[2] = cmd;
  buf[3] = (len >> 0) & 0xFF;
  buf[4] = (len >> 8) & 0xFF;
  if(len > 0)memcpy(&buf[5], data, len);
  uint8_t sum = 0;
  for(int i=2; i<(len+5); i++)sum += buf[i];
  buf[len+5] = sum;
  
  if( interface->debug )interface->debug(interface, "uart_if_send", len+6, buf);
  
  int res = wch_if_uart_write(TTY, buf, len+6);
  if(res != (len+6)){fprintf(stderr, "uart_if_send error\n"); return 0;}
  return res;
}

//  received buffer format:
//    [0] = 0x55
//    [1] = 0xAA
//    [2] = cmd
//    [3] = 0
//    [4:5] = len
//    [6 .. len+5] = data
//    [len+6] = checksum
static size_t uart_if_recv(wch_if_t interface, uint8_t cmd, uint16_t len, uint8_t data[]){
  uint8_t buf[67];
  int res;
  uint16_t datalen;
  uint8_t sum = 0;
  res = wch_if_uart_read(TTY, buf, 6);
  if(res != 6){fprintf(stderr, "uart_if_recv: timeout\n"); return 0;}
  datalen = (uint16_t)buf[5]<<8 | buf[4];
  
  res = wch_if_uart_read(TTY, &buf[6], datalen+1);
  if(res != (datalen+1)){fprintf(stderr, "uart_if_recv: timeout\n"); return 0;}
  
  if(buf[0] != 0x55 || buf[1] != 0xAA)printf("uart_if_recv: preamble: fail\n");
  
  if(buf[2] != cmd){
    fprintf(stderr, "uart_if_recv: got wrong 'cmd' %.2X (exp %.2X)\n", buf[2], cmd);
    return 0;
  }

  for(int i=2; i<(datalen+6); i++)sum+=buf[i];
  if(buf[datalen+6] != sum)printf("uart_if_recv: checksum error: %.2X (exp. %.2X)\n", buf[datalen+6], sum);
  
  if( interface->debug )interface->debug(interface, "uart_if_send", len+7, buf);
  
  if(len > datalen)len = datalen;
  if(data != NULL)memcpy(data, &buf[6], len);
  return len;
}

static void uart_if_close(wch_if_t *interface){
  wch_if_uart_t tty = ( (wch_if_uart_t)((*interface)->intern) );
  wch_if_uart_close(tty);
  
  free(*interface);
  *interface = NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
//        TTY implementation
///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////

#if defined(linux) || defined(__linux) || defined(__linux__) || defined(__GNU__) || defined(__GLIBC__)
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <termios.h>
#include <stdlib.h>

struct wch_if_uart{
  int fd;
  struct termios ttyset, oldset;
};

//I know it looks like a crutch, but I can’t guarantee that B9600 == 9600
static speed_t speed_convert(unsigned int baudrate){
  switch(baudrate){
    case 0: return B0;
    case 50: return B50;
    case 75: return B75;
    case 110: return B110;
    case 134: return B134;
    case 150: return B150;
    case 200: return B200;
    case 300: return B300;
    case 600: return B600;
    case 1200: return B1200;
    case 1800: return B1800;
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
#ifdef B460800
    case 460800: return B460800;
#endif
#ifdef B500000
    case 500000: return B500000;
#endif
#ifdef B576000
    case 576000: return B576000;
#endif
#ifdef B921600
    case 921600: return B921600;
#endif
#ifdef B1000000
    case 1000000: return B1000000;
#endif
#ifdef B1152000
    case 1152000: return B1152000;
#endif
#ifdef B1500000
    case 1500000: return B1500000;
#endif
#ifdef B2000000
    case 2000000: return B2000000;
#endif
#ifdef B2500000
    case 2500000: return B2500000;
#endif
#ifdef B3000000
    case 3000000: return B3000000;
#endif
#ifdef B3500000
    case 3500000: return B3500000;
#endif
#ifdef B4000000
    case 4000000: return B4000000;
#endif
    default: return B0;
  }
}

wch_if_uart_t wch_if_uart_open(char name[], unsigned int baudrate){
  speed_t speed = speed_convert(baudrate);
  wch_if_uart_t res = (struct wch_if_uart*)malloc(sizeof(struct wch_if_uart));
  if(res == NULL)return res;
  res->fd = open(name, O_RDWR | O_NOCTTY);
  if(res->fd == -1){
    free(res);
    return NULL;
  }
  tcgetattr( res->fd, &(res->ttyset) );
  memcpy( &(res->oldset), &(res->ttyset), sizeof(struct termios));
  res->ttyset.c_cflag = speed | CS8 | CLOCAL | CREAD;
  res->ttyset.c_iflag = IGNPAR;
  res->ttyset.c_oflag = 0;
  res->ttyset.c_lflag = 0;
  res->ttyset.c_cc[VMIN] = 0;
  res->ttyset.c_cc[VTIME]= 0;
  tcflush(res->fd, TCIFLUSH);
  tcsetattr(res->fd, TCSANOW, &(res->ttyset) );
  return res;
}

int wch_if_uart_close(wch_if_uart_t tty){
  if(tty == NULL)return -1;
  tcsetattr( tty->fd, TCSANOW, &(tty->oldset) );
  close( tty->fd );
  free(tty);
  return 0;
}

ssize_t wch_if_uart_write(wch_if_uart_t tty, void *buf, size_t count){
  if(tty == NULL)return -1;
  return write( tty->fd, buf, count );
}

ssize_t wch_if_uart_read(wch_if_uart_t tty, void *buf, size_t count){
  if(tty == NULL)return -1;
  if((count > 1) && (tty->ttyset.c_cc[VTIME]>0)){
    tty->ttyset.c_cc[VMIN]= count;
    tcsetattr( tty->fd, TCSANOW, &(tty->ttyset) );
  }
  return read( tty->fd, buf, count );
}

int wch_if_uart_timeout(wch_if_uart_t tty, ssize_t time_ms){
  if(tty == NULL)return -1;
  time_ms /= 100;
  tty->ttyset.c_cc[VTIME]= time_ms;
  if(time_ms == 0)tty->ttyset.c_cc[VMIN] = 0;
  tcsetattr( tty->fd, TCSANOW, &(tty->ttyset) );
  return 0;
}

void wch_if_uart_rts(wch_if_uart_t tty, char state){
  int flags;
  state = !state;
  ioctl(tty->fd, TIOCMGET, &flags);
  flags = (flags &~TIOCM_RTS) | (state*TIOCM_RTS);
  ioctl(tty->fd, TIOCMSET, &flags);
}

void wch_if_uart_dtr(wch_if_uart_t tty, char state){
  int flags;
  state = !state;
  ioctl(tty->fd, TIOCMGET, &flags);
  flags = (flags &~TIOCM_DTR) | (state*TIOCM_DTR);
  ioctl(tty->fd, TIOCMSET, &flags);
}

#elif defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#include <ioapiset.h>
#ifndef PHYSICAL_ADDRESS //bug workaround "/usr/share/mingw-w64/include/ntddser.h:368:9: error: unknown type name ‘PHYSICAL_ADDRESS’ "
typedef LARGE_INTEGER PHYSICAL_ADDRESS, *PPHYSICAL_ADDRESS;
#endif
#include <ntddser.h>


struct wch_if_uart{
  HANDLE handle;
};

wch_if_uart_t wch_if_uart_open(char name[], unsigned int baudrate){
  wch_if_uart_t res = (struct wch_if_uart*)malloc(sizeof(struct wch_if_uart));
  if(res == NULL)return res;
  res->handle = CreateFileA(name,
                GENERIC_READ | GENERIC_WRITE,
                0,
                NULL,
                OPEN_EXISTING, 
                FILE_ATTRIBUTE_NORMAL,
                NULL);
  if(res->handle == (HANDLE)-1){
    free(res);
    return NULL;
  }
  
  SetCommMask(res->handle, EV_RXCHAR); //какие сигналы отслеживать (только RX)
  SetupComm(res->handle, 1000, 1000); //размер буферов на прием и передачу

  COMMTIMEOUTS timeout;
  timeout.ReadIntervalTimeout = 0;
  timeout.ReadTotalTimeoutMultiplier = 1;
  timeout.ReadTotalTimeoutConstant = 0;
  timeout.WriteTotalTimeoutMultiplier = 1;
  timeout.WriteTotalTimeoutConstant = 0;

  if( !SetCommTimeouts(res->handle, &timeout) ){
    CloseHandle(res->handle);
    free(res);
    return NULL;
  }

  DCB dcb;

  memset(&dcb,0,sizeof(DCB));
  dcb.DCBlength = sizeof(DCB);
  GetCommState(res->handle, &dcb);
  
  dcb.BaudRate = (DWORD)baudrate;
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  dcb.fAbortOnError = FALSE;
  dcb.fDtrControl = DTR_CONTROL_ENABLE; //DTR_CONTROL_DISABLE;
  dcb.fRtsControl = RTS_CONTROL_ENABLE; //RTS_CONTROL_DISABLE;
  dcb.fBinary = TRUE;
  dcb.fParity = FALSE;
  dcb.fInX = FALSE;
  dcb.fOutX = FALSE;
  dcb.XonChar = 0;
  dcb.XoffChar = (unsigned char)0xFF;
  dcb.fErrorChar = FALSE;
  dcb.fNull = FALSE;
  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.XonLim = 128;
  dcb.XoffLim = 128;

  if( !SetCommState(res->handle, &dcb) ){
    CloseHandle(res->handle);
    free(res);
    return NULL;
  }
  return res;
}

int wch_if_uart_close(wch_if_uart_t tty){
  CloseHandle(tty->handle);
  free(tty);
  return 0;
}

ssize_t wch_if_uart_write(wch_if_uart_t tty, void *buf, size_t count){
  DWORD cnt;
  if( !WriteFile(tty->handle, buf, (DWORD)count, &cnt, NULL))return -1;
  return cnt;
}

ssize_t wch_if_uart_read(wch_if_uart_t tty, void *buf, size_t count){
  DWORD cnt;
  if( !ReadFile(tty->handle, buf, (DWORD)count, &cnt, NULL))return -1;
  return cnt;
}

int wch_if_uart_timeout(wch_if_uart_t tty, ssize_t time_ms){
  COMMTIMEOUTS timeout;
  GetCommTimeouts(tty->handle, &timeout);
  timeout.ReadIntervalTimeout = time_ms;
  timeout.ReadTotalTimeoutMultiplier = 1;
  timeout.ReadTotalTimeoutConstant = time_ms;
  timeout.WriteTotalTimeoutMultiplier = 1;
  timeout.WriteTotalTimeoutConstant = time_ms;
  if( !SetCommTimeouts(tty->handle, &timeout) ){
    return -1;
  }
  return 0;
}

void wch_if_uart_rts(wch_if_uart_t tty, char state){
  DWORD code;
  if(state)code = IOCTL_SERIAL_CLR_RTS; else code = IOCTL_SERIAL_SET_RTS;
  DeviceIoControl(tty->handle, code, NULL, 0, NULL, 0, NULL, NULL);
}

void wch_if_uart_dtr(wch_if_uart_t tty, char state){
  DWORD code;
  if(state)code = IOCTL_SERIAL_CLR_DTR; else code = IOCTL_SERIAL_SET_DTR;
  DeviceIoControl(tty->handle, code, NULL, 0, NULL, 0, NULL, NULL);
}

#else
  #error "Unsupported platform"
#endif