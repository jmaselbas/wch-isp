/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: 2022 Jules Maselbas */
/* SPDX-FileCopyrightText: 2022 Benjamin Vernoux */
#include <unistd.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <libusb.h>

static void usage(void);
#include "arg.h"

#define __noreturn __attribute__((noreturn))

//#define DEBUG

#ifdef DEBUG
#define dbg_printf printf
#else
#define dbg_printf(...) do { ; } while(0)
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define BIT(x)		(1UL << (x))
#define MIN(a, b)	((a) < (b) ? (a) : (b))
#define MAX(a, b)	((a) < (b) ? (b) : (a))
#define LEN(a)		(sizeof(a) / sizeof(*a))
#define ALIGN(x, a)	(((x) + ((a) - 1)) & ~((a) - 1))

#include "devices.h"

#define MAX_PACKET_SIZE 64
#define SECTOR_SIZE  1024

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
#define CFG_MASK_RDPR_USER_DATA_WPR 0x07
#define CFG_MASK_BTVER 0x08 /* Bootloader version, in the format of `[0x00, major, minor, 0x00]` */
#define CFG_MASK_UID 0x10 /* Device Unique ID */
#define CFG_MASK_ALL 0x1f /* All mask bits of CFGs */

#define CMD_IDENTIFY	    0xa1
#define CMD_ISP_END	        0xa2
#define CMD_ISP_KEY	        0xa3
#define CMD_ERASE	        0xa4
#define CMD_PROGRAM	        0xa5
#define CMD_VERIFY	        0xa6
#define CMD_READ_CONFIG	    0xa7
#define CMD_WRITE_CONFIG	0xa8
#define CMD_DATA_ERASE	    0xa9
#define CMD_DATA_PROGRAM	0xaa
#define CMD_DATA_READ	    0xab
#define CMD_WRITE_OTP	    0xc3
#define CMD_READ_OTP	    0xc4
#define CMD_SET_BAUD	    0xc5

#define BTVER_2_7 (0x00020700)

#define ISP_VID 0x4348
#define ISP_PID 0x55e0
#define ISP_EP_OUT (2 | LIBUSB_ENDPOINT_OUT)
#define ISP_EP_IN (2 | LIBUSB_ENDPOINT_IN)

__noreturn static void die(const char *errstr, ...);
__noreturn static void version_print(void);
__noreturn static void usage(void);

static void usb_init(void);
static void usb_fini(void);
static size_t usb_send(size_t len, u8 *buf);
static size_t usb_recv(size_t len, u8 *buf);

static void isp_init(void);
static void isp_fini(void);
static size_t isp_send_cmd(u8 cmd, u16 len, u8 *data);
static size_t isp_recv_cmd(u8 cmd, u16 len, u8 *data);

libusb_context *usb = NULL;
libusb_device_handle *dev = NULL;
unsigned int interface;
unsigned int kernel;

#define CURR_TIME_SIZE (40)
char currTime[CURR_TIME_SIZE+1] = "";
struct timeval start_tv;
struct timeval curr_tv;

void *flash_file_bin = NULL;
FILE *flash_file_fp = NULL;

float TimevalDiff(const struct timeval *a, const struct timeval *b)
{
	return (a->tv_sec - b->tv_sec) + 1e-6f * (a->tv_usec - b->tv_usec);
}

void get_CurrentTime(char* date_time_ms, int date_time_ms_max_size)
{
	#define CURRENT_TIME_SIZE (30)
	char currentTime[CURRENT_TIME_SIZE+1] = "";
	time_t rawtime;
	struct tm * timeinfo;

	struct timeval curTime;
	gettimeofday(&curTime, NULL);
	int milli = curTime.tv_usec / 1000;

	time (&rawtime);
	timeinfo = localtime(&rawtime);

	strftime(currentTime, CURRENT_TIME_SIZE, "%Y-%m-%d %H:%M:%S", timeinfo);
	snprintf(date_time_ms, (date_time_ms_max_size-1), "%s.%03d", currentTime, milli);
}

void printf_timing(const char *fmt, ...)
{
	va_list args;

	gettimeofday(&curr_tv, NULL);
	get_CurrentTime(currTime, CURR_TIME_SIZE);
	printf("%s (%05.03f s) ", currTime, TimevalDiff(&curr_tv, &start_tv));

	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}

void print_hex(uint8_t* data, uint8_t size)
{
	uint8_t ascii[17];
	uint8_t i, j;
	ascii[16] = '\0';
	for (i = 0; i < size; ++i) {
		printf("%02X ", data[i]);
		if (data[i] >= 0x20 && data[i] <= 0x7f) {
			ascii[i % 16] = data[i];
		} else {
			ascii[i % 16] = '.';
		}
		if ((i+1) % 8 == 0 || i+1 == size) {
			printf(" ");
			if ((i+1) % 16 == 0) {
				printf("|  %s \r\n", ascii);
			} else if (i+1 == size) {
				ascii[(i+1) % 16] = '\0';
				if ((i+1) % 16 <= 8) {
					printf(" ");
				}
				for (j = (i+1) % 16; j < 16; ++j) {
					printf("   ");
				}
				printf("|  %s \r\n", ascii);
			}
		}
	}
}

static void
die(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);

	usb_fini();
	exit(1);
}

static size_t
usb_send(size_t len, u8 *buf)
{
	int got = 0;
	int ret = libusb_bulk_transfer(dev, ISP_EP_OUT, buf, len, &got, 10000);
	if (ret)
		die("send failed: %s\n", libusb_strerror(ret));
	return got;
}

static size_t
usb_recv(size_t len, u8 *buf)
{
	int got = 0;
	int ret = libusb_bulk_transfer(dev, ISP_EP_IN, buf, len, &got, 10000);
	if (ret)
		die("recv failed: %s\n", libusb_strerror(ret));
	if (got < 0)
		die("recv failed: got %d\n", got);
	if (got > len)
		got = len;
	return got;
}

static size_t
isp_send_cmd(u8 cmd, u16 len, u8 *data)
{
	u8 buf[64];

	if ((len + 3) > sizeof(buf))
		die("isp_send_cmd: invalid argument, length %d\n", len);

	buf[0] = cmd;
	/* length is sent in little endian... but it doesn't really matter
	 * as the usb maxpacket size is 64, thus len should never be greater
	 * than 61 (64 minus the 3 bytes header). */
	buf[1] = (len >> 0) & 0xff;
	buf[2] = (len >> 8) & 0xff;
	if (len != 0)
		memcpy(&buf[3], data, len);

	dbg_printf("isp send cmd %.2x len %.2x%.2x:\n", buf[0], buf[2], buf[1]);
#ifdef DEBUG
	print_hex(data, len);
#endif

	return usb_send(len + 3, buf);
}

static size_t
isp_recv_cmd(u8 cmd, u16 len, u8 *data)
{
	u8 buf[64];
	size_t got;
	u16 hdrlen;

	if ((len + 4) > sizeof(buf))
		die("isp_recv_cmd: invalid argument, length %d\n", len);
	got = usb_recv(len + 4, buf);

	if (got < 4)
		die("isp_recv_cmd: not enough data recv\n");

	if (buf[0] != cmd)
		die("isp_recv_cmd: got wrong command %#.x (exp %#.x)\n", buf[0], cmd);
	if (buf[1])
		die("isp_recv_cmd: cmd error %#.x\n", buf[1]);

	got -= 4;
	hdrlen = buf[2] | (buf[3] << 8);
	if (hdrlen != got)
		die("isp_recv_cmd: length mismatch, got %#.x (hdr %#.x)\n", got, hdrlen);
	len = MIN(len, got);

	if (data != NULL)
		memcpy(data, buf + 4, len);

	dbg_printf("isp recv cmd %.2x status %.2x len %.2x%.2x:\n", buf[0], buf[1], buf[3], buf[2]);

#ifdef DEBUG
	print_hex(data, len);
#endif
	return got;
}

static void
cmd_identify(u8 *dev_id, u8 *dev_type)
{
	u8 buf[] = "\0\0MCU ISP & WCH.CN";
	u8 ids[2];

	buf[0] = 0; // dev_id
	buf[1] = 0; // dev_type

#ifdef DEBUG
	printf("cmd_identify isp_send_cmd()/isp_recv_cmd()\n");
#endif

	/* do not send the terminating nul byte, hence sizeof(buf) - 1 */
	isp_send_cmd(CMD_IDENTIFY, sizeof(buf) - 1, buf);
	isp_recv_cmd(CMD_IDENTIFY, sizeof(ids), ids);

	if (dev_id)
		*dev_id = ids[0];
	if (dev_type)
		*dev_type = ids[1];

#ifdef DEBUG
	printf("\n");
#endif
}

static void
cmd_isp_key(size_t len, u8 *key, u8 *sum)
{
	u8 rsp[2];

#ifdef DEBUG
	printf("cmd_isp_key isp_send_cmd()/isp_recv_cmd()\n");
#endif

	isp_send_cmd(CMD_ISP_KEY, len, key);
	isp_recv_cmd(CMD_ISP_KEY, sizeof(rsp), rsp);
	if (sum)
		*sum = rsp[0];

#ifdef DEBUG
	printf("\n");
#endif
}

static void
cmd_isp_end(u8 reason)
{
	u8 buf[2];
#ifdef DEBUG
	printf("cmd_isp_end isp_send_cmd()/isp_recv_cmd()\n");
#endif

	isp_send_cmd(CMD_ISP_END, sizeof(reason), &reason);
	if (reason == 0) /* the device is not expected to respond */
		isp_recv_cmd(CMD_ISP_END, sizeof(buf), buf);

#ifdef DEBUG
	printf("\n");
#endif
}

static void
cmd_erase(u32 sectors)
{
	u8 sec[4];

	sec[0] = (sectors >>  0) & 0xff;
	sec[1] = (sectors >>  8) & 0xff;
	sec[2] = (sectors >> 16) & 0xff;
	sec[3] = (sectors >> 24) & 0xff;

	isp_send_cmd(CMD_ERASE, sizeof(sec), sec);
	isp_recv_cmd(CMD_ERASE, 2, sec); /* receive two 0 bytes */
}

static size_t
cmd_program(uint32_t addr, size_t len, const u8 *data, const u8 key[8])
{
	u8 unk[61];
	u8 rsp[2];
	size_t i;

	unk[0] = (addr >>  0) & 0xff;
	unk[1] = (addr >>  8) & 0xff;
	unk[2] = (addr >> 16) & 0xff;
	unk[3] = (addr >> 24) & 0xff;
	unk[4] = 0; /* carefully choosen random number */

	len = MIN(sizeof(unk) - 5, len);
	for (i = 0; i < len; i++)
		unk[5 + i] = data[i] ^ key[i % 8];

	isp_send_cmd(CMD_PROGRAM, len + 5, unk);
	isp_recv_cmd(CMD_PROGRAM, sizeof(rsp), rsp);

	if (rsp[0] != 0 || rsp[1] != 0)
		die("Fail to program chunk @ %#x error: %.2x %.2x\n", addr, rsp[0], rsp[1]);

	return len;
}

static size_t
cmd_verify(uint32_t addr, size_t len, const u8 *data, const u8 key[8])
{
	u8 unk[61];
	u8 rsp[2];
	size_t i;

	unk[0] = (addr >>  0) & 0xff;
	unk[1] = (addr >>  8) & 0xff;
	unk[2] = (addr >> 16) & 0xff;
	unk[3] = (addr >> 24) & 0xff;
	unk[4] = 0; /* carefully choosen random number */

	len = MIN(sizeof(unk) - 5, len);
	for (i = 0; i < len; i++)
		unk[5 + i] = data[i] ^ key[i % 8];

	isp_send_cmd(CMD_VERIFY, len + 5, unk);
	isp_recv_cmd(CMD_VERIFY, sizeof(rsp), rsp);

	if (rsp[0] != 0 || rsp[1] != 0)
		die("Fail to verify chunk @ %#x error: %.2x %.2x (len=%lld)\n", addr, rsp[0], rsp[1], len);

	return len;
}

static size_t
cmd_read_conf(u16 cfgmask, size_t len, u8 *cfg)
{
	u8 buf[60];
	u8 req[2];
	u16 mask;
	size_t got;

	req[0] = (cfgmask >>  0) & 0xff;
	req[1] = (cfgmask >>  8) & 0xff;
#ifdef DEBUG
	printf("cmd_read_conf isp_send_cmd()/isp_recv_cmd()\n");
#endif

	isp_send_cmd(CMD_READ_CONFIG, sizeof(req), req);
	got = isp_recv_cmd(CMD_READ_CONFIG, sizeof(buf), buf);
	if (got < 2)
		die("read conf fail: not received enough bytes\n");
	mask = buf[0] | (buf[1] << 8);
	if (cfgmask != mask)
		die("read conf fail: received conf does not match\n");
	len = MIN(got - 2, len);
	memcpy(cfg, &buf[2], len);

	return len;
}

u8 cmd_debug_mode(u8 enable)
{
	u8 enable_debug_req[] = {
		0x07, 0x00,
		0x11, 0xbf, 0xf9, 0xf7,
		0x13, 0xbf, 0xf9, 0xec,
		0xe5,
		0xf2,
		0xff, 0x8f
	};

	u8 disable_debug_req[] = {
	    0x07, 0x00,
        0x11, 0xbf, 0xf9, 0xf7,
        0x13, 0xbf, 0xf9, 0xec,
        0x45,
        0xf2,
        0xff, 0x8f
	};

	u8 buf[6];
	size_t got;

#ifdef DEBUG
	printf("cmd_enable_debug_mode isp_send_cmd()\n");
#endif

	u8 *req = enable ? enable_debug_req : disable_debug_req;

	isp_send_cmd(CMD_WRITE_CONFIG, sizeof(enable_debug_req), req);
	got = usb_recv(sizeof(buf), buf);
	if (got != 6)
		die("enable debug command: wrong response length\n");

#ifdef DEBUG
	print_hex(buf, 6);
#endif
	return buf[4] == 0; // success if this byte is zero
}

static u32 read_btver(void)
{
	u8 buf[4];

#ifdef DEBUG
	printf("read_btver\n");
#endif
	/* format: [major, major, minor, minor] */
	cmd_read_conf(CFG_MASK_BTVER, sizeof(buf), buf);

	return ((buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]);
}

static void
usb_init(void)
{
	int err;
	const struct libusb_version* version;

	version = libusb_get_version();
	printf_timing("Using libusb v%d.%d.%d.%d\n", version->major, version->minor, version->micro, version->nano);

	err = libusb_init(&usb);
	if (err)
		die("libusb_init: %s\n", libusb_strerror(err));

	dev = libusb_open_device_with_vid_pid(usb, ISP_VID, ISP_PID);
	if (!dev)
		die("Fail to open device %4x:%4x %s\n", ISP_VID, ISP_PID,
		    strerror(errno));
#ifdef __linux__
	kernel = libusb_kernel_driver_active(dev, 0);
	if (kernel == 1)
	{
		if (libusb_detach_kernel_driver(dev, 0))
			die("Couldn't detach kernel driver!\n");
	}
#endif
	err = libusb_claim_interface(dev, 0);
	if (err)
		die("libusb_claim_interface: %s\n", libusb_strerror(err));
}

static void
usb_fini(void)
{
	int err = 0;
	if(flash_file_fp)
		fclose(flash_file_fp);
	if(flash_file_bin)
		free(flash_file_bin);
	if (dev)
		err = libusb_release_interface(dev, 0);
	if (err)
		fprintf(stderr, "libusb_release_interface: err=%d %s\n", err, libusb_strerror(err));
#ifdef __linux__
	if (kernel == 1)
		libusb_attach_kernel_driver(dev, 0);
#endif
	if(dev)
		libusb_close(dev);
	if (usb)
		libusb_exit(usb);
}

static u8 dev_id;
static u8 dev_type;
static u8 dev_uid[8];
static u32 dev_btver;
static size_t dev_uid_len;
static u8 isp_key[30]; /* all zero key */
static u8 xor_key[8];

static struct dev *dev_db;

static int do_progress;
static int do_reset;
static int do_verify;
static int do_show_config;

static size_t
db_flash_size(void)
{
	if (dev_db && dev_db->flash_size > 0)
		return dev_db->flash_size;
	return -1;
}

static size_t
db_flash_sector_size(void)
{
	if (dev_db && dev_db->flash_sector_size > 0)
		return dev_db->flash_sector_size;
	return SECTOR_SIZE;
}

static void
isp_init(void)
{
	size_t i;
	u8 sum;
	u8 rsp;

	/* get the device type and id */
	cmd_identify(&dev_id, &dev_type);

	/* match the detected device */
	for (i = 0; i < LEN(devices); i++)
	{
		if (devices[i].type == dev_type && devices[i].id == dev_id)
		{
			dev_db = &devices[i];
			break;
		}
	}
	if (dev_db)
	{
		printf_timing("Found chip: %s [0x%.2x%.2x] Flash %dK\n", dev_db->name,
		       dev_type, dev_id, dev_db->flash_size / SZ_1K);
	} else {
		printf_timing("Unknown chip: [0x%.2x%.2x]", dev_type, dev_id);
	}

	/* get the device uid */
	dev_uid_len = cmd_read_conf(CFG_MASK_UID, sizeof(dev_uid), dev_uid);
	printf_timing("Chip uid: ");
	for (i = 0; i < dev_uid_len; i++)
		printf("%.2X ", dev_uid[i]);
	puts("");

	/* get the bootloader version */
	dev_btver = read_btver();
	printf_timing("bootloader: v%d%d.%d%d (0x%08X)\n", 
		dev_btver >> 24, ((dev_btver & 0x00FF0000) >> 16), 
		((dev_btver & 0x0000FF00) >> 8), dev_btver & 0xFF, 
		dev_btver);

	/* initialize xor_key */
	for (sum = 0, i = 0; i < dev_uid_len; i++)
		sum += dev_uid[i];
	memset(xor_key, sum, sizeof(xor_key));
	xor_key[7] = xor_key[0] + dev_id;

	/* send the isp key */
	cmd_isp_key(sizeof(isp_key), isp_key, &rsp);

	if (dev_btver >= BTVER_2_7) {
		/* bootloader version 2.7 (and maybe onward) simply send zero */
		sum = 0;
	} else {
		/* bootloader version 2.6 (and maybe prior versions) send back
		 * the a checksum of the xor_key. This response can be used to
		 * make sure we are in sync. */
		for (sum = 0, i = 0; i < sizeof(xor_key); i++)
			sum += xor_key[i];
	}
	if (rsp != sum)
		die("failed set isp key, wrong reply, got %x (exp %x)\n", rsp, sum);
}

static void
progress_bar(const char *act, size_t current, size_t total)
{
	static char f[] = "####################################################";
	static char e[] = "                                                    ";
	int l = strlen(f);
	int n = (current * l) / total;

	if (!do_progress)
		return;
	printf_timing("\r[%.*s%.*s] %s %lld/%lld", n, f, l - n, e, act, current, total);
	if (current == total)
		puts("");
	fflush(stdout);
}

static void
isp_flash(size_t size, u8 *data)
{
	size_t sector_size = db_flash_sector_size();
	u32 nr_sectors = ALIGN(size, sector_size) / sector_size;
	u32 max_sectors =  dev_db->flash_size / sector_size;
	size_t off = 0;
	size_t rem = size;
	size_t len;

	printf_timing("isp_flash start (max sectors=%d)\n", max_sectors);
	nr_sectors = MIN(nr_sectors, max_sectors);
	printf_timing("cmd_erase %d sectors start\n", nr_sectors);
	cmd_erase(nr_sectors);
	printf_timing("cmd_erase end\n");

	printf_timing("cmd_program start\n");
	while (off < size) {
		progress_bar("write", off, size);

		len = cmd_program(off, rem, data + off, xor_key);
		off += len;
		rem -= len;
	}
	cmd_program(off, 0, NULL, xor_key);
	progress_bar("write", size, size);
	printf_timing("cmd_program end with success\n");
	printf_timing("isp_flash end\n");
}

static void
isp_verify(size_t size, u8 *data)
{
	size_t off = 0;
	size_t rem = size;
	size_t len;

	printf_timing("isp_verify start\n");
	while (off < size) {
		progress_bar("verify", off, size);

		len = cmd_verify(off, rem, data + off, xor_key);
		off += len;
		rem -= len;
	}
	progress_bar("verify", size, size);
	printf_timing("isp_verify end with success\n");
}

static void
isp_fini(void)
{
	if (do_reset)
		cmd_isp_end(1);
}

static size_t
f_size(FILE *fp)
{
    size_t prev=ftell(fp);
    fseek(fp, 0L, SEEK_END);
    size_t sz=ftell(fp);
    fseek(fp,prev,SEEK_SET); //go back to where we were
    return sz;
}

static void
flash_file(const char *name)
{
	size_t size;
	size_t size_align;
	size_t ret;

	flash_file_fp = fopen(name, "rb");
	if (flash_file_fp == NULL)
		die("%s: %s\n", name, strerror(errno));

	size = f_size(flash_file_fp);
	size_align = ALIGN(size, 64);
	if (size_align > db_flash_size())
		die("%s: file too big, flash size is %d", name, db_flash_size());

	flash_file_bin = malloc(size_align);
	if (flash_file_bin == NULL)
	{
		die("flash_file Memory error\n");
	}
	memset(flash_file_bin, 0, size_align);
	// Copy the file into the buffer flash_file_bin
	ret = fread(flash_file_bin, 1, size, flash_file_fp);
	if (ret != size)
	{
		die("flash_file reading error\n");
	}
	printf_timing("Flash file: %s\n", name);
	printf_timing("File length: %lld (size aligned: %lld)\n", size, size_align);
	isp_flash(size_align, flash_file_bin);
	if (do_verify)
		isp_verify(size_align, flash_file_bin);

	/* Note flash_file_fp & flash_file_bin handles are closed/freed by usb_fini() */
}

char *argv0;

static void
ch569_print_config(size_t len, u8 *cfg)
{
	u32 nv;

	if (len < 12)
		return;
	/*
	printf("cfg(Hex)=");	
	for(int i = 0; i < 12; i++)
		printf("%02X ", cfg[i]);	
	printf("\n");
	*/
	nv = (cfg[8] << 0) | (cfg[9] << 8) | (cfg[10] << 16) | (cfg[11] << 24);
	printf("nv=0x%08X\n", nv);
	printf("[4] RESET_EN %d: %s\n", !!(nv & BIT(4)),
	       (nv & BIT(4)) ? "enabled" : "disabled");
	printf("[5] DEBUG_EN %d: %s\n", !!(nv & BIT(5)),
	       (nv & BIT(5)) ? "enabled" : "disabled");
	printf("[6] BOOT_EN %d: %s\n", !!(nv & BIT(6)),
	       (nv & BIT(6)) ? "enabled" : "disabled");
	printf("[7] CODE_READ_EN %d: %s\n", !!(nv & BIT(7)),
	       (nv & BIT(7)) ? "enabled" : "disabled");
	printf("[29] LOCKUP_RST_EN %d: %s\n", !!(nv & BIT(29)),
	       (nv & BIT(29)) ? "enabled" : "disabled");
	printf("[31:30] USER_MEM 0x%02X: %s\n", (nv >> 30) & 0b11,
	       ((nv >> 30) & 0b11) == 0 ? "RAMX 32KB + ROM 96KB" :
	       ((nv >> 30) & 0b11) == 1 ? "RAMX 64KB + ROM 64KB" :
	       "RAMX 96KB + ROM 32KB");
}

static void
config_show(void)
{
	u8 cfg[16];
	size_t len;

	len = cmd_read_conf(0x7, sizeof(cfg), cfg);
	ch569_print_config(len, cfg);
/*
	for (int i = 0; i < len; i++)
		printf("%.2x%c", cfg[i], ((i % 4) == 3) ? '\n' : ' ');
*/
}

static void
usage(void)
{
	printf("usage: %s [-Vprvc] COMMAND [ARG ...]\n", argv0);
	printf("       %s [-Vprvc] flash|debug-on|debug-off FILE\n", argv0);
	printf("-V means print version\n");
	printf("-p means display progress\n");
	printf("-r means do reset at end\n");
	printf("-v means do verify after erase/program\n");
	printf("-c means print CH569 config(after isp_init and before isp_fini)\n");
	die("");
}

static void
version_print(void)
{
	printf("%s v%s\n", argv0, VERSION);
	die("");
}

int
main(int argc, char *argv[])
{
	argv0 = argv[0];

	ARGBEGIN {
	case 'p':
		do_progress = 1;
		break;
	case 'r':
		do_reset = 1;
		break;
	case 'v':
		do_verify = 1;
		break;
	case 'c':
		do_show_config = 1;
		break;
	case 'V':
		version_print();
	default:
		usage();
	} ARGEND;

	printf("%s v%s\n", argv0, VERSION);
	gettimeofday(&start_tv, NULL);
	get_CurrentTime(currTime, CURR_TIME_SIZE);
	printf("Start time %s\n", currTime);
	usb_init();
	isp_init();

	if (do_show_config)
		config_show();

	if (argc < 1)
		die("missing command\n");
	if (strcmp(argv[0], "flash") == 0) {
		if (argc < 2)
			die("flash: missing file\n");
		flash_file(argv[1]);
	}

	if (strcmp(argv[0], "debug-on") == 0) {
		if (dev_id != 0x69)
			die("This feature is currently only available for the CH569!");

		if (cmd_debug_mode(1)) {
			printf("successfully enabled debug mode.\n");
		} else {
			printf("failed to enable debug mode.\n");
		}
	}

	if (strcmp(argv[0], "debug-off") == 0) {
		if (dev_id != 0x69)
			die("This feature is currently only available for the CH569!");

		if (cmd_debug_mode(0)) {
			printf("successfully disabled debug mode.\n");
		} else {
			printf("failed to disable debug mode.\n");
		}
	}

	if (do_show_config)
		config_show();

	isp_fini();
	usb_fini();
	gettimeofday(&curr_tv, NULL);
	get_CurrentTime(currTime, CURR_TIME_SIZE);
	printf("End time %s (Duration %05.03f s)\n", currTime, TimevalDiff(&curr_tv, &start_tv));

	return 0;
}
