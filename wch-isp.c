/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: 2022 Jules Maselbas */
#include <unistd.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <libusb.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

static void usage(void);
#include "arg.h"

#ifdef DEBUG
#define dbg_printf printf
#else
#define dbg_printf(...) do { ; } while(0)
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define MIN(a,b)	((a) < (b) ? (a) : (b))
#define MAX(a,b)	((a) < (b) ? (b) : (a))
#define LEN(a) (sizeof(a) / sizeof(*a))

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

#define CMD_IDENTIFY	0xa1
#define CMD_ISP_END	0xa2
#define CMD_ISP_KEY	0xa3
#define CMD_ERASE	0xa4
#define CMD_PROGRAM	0xa5
#define CMD_VERIFY	0xa6
#define CMD_READ_CONFIG	0xa7
#define CMD_WRITE_CONFIG	0xa8
#define CMD_DATA_ERASE	0xa9
#define CMD_DATA_PROGRAM	0xaa
#define CMD_DATA_READ	0xab
#define CMD_WRITE_OTP	0xc3
#define CMD_READ_OTP	0xc4
#define CMD_SET_BAUD	0xc5

#define ISP_VID 0x4348
#define ISP_PID 0x55e0
#define ISP_EP_OUT (2 | LIBUSB_ENDPOINT_OUT)
#define ISP_EP_IN (2 | LIBUSB_ENDPOINT_IN)

static void die(const char *errstr, ...);
static void usb_init(void);
static void usb_fini(void);
static size_t usb_send(size_t len, u8 *buf);
static size_t usb_recv(size_t len, u8 *buf);

static void isp_init(void);
static void isp_fini(void);
static size_t isp_send_cmd(u8 cmd, u16 len, u8 *data);
static size_t isp_recv_cmd(u8 cmd, u16 len, u8 *data);

libusb_context *usb;
libusb_device_handle *dev;
unsigned int interface;
unsigned int kernel;

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

	dbg_printf("isp send cmd %.2x len %.2x%.2x : ", buf[0], buf[2], buf[1]);
	for (int i = 0; i < len; i++)
		dbg_printf("%.2x", data[i]);
	dbg_printf("\n");

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

	dbg_printf("isp recv cmd %.2x status %.2x len %.2x%.2x : ", buf[0], buf[1], buf[3], buf[2]);
	for (int i = 0; i < len; i++)
		dbg_printf("%.2x", data[i]);
	dbg_printf("\n");

	return got;
}

static void
cmd_identify(u8 *dev_id, u8 *dev_type)
{
	u8 buf[] = "\0\0MCU ISP & WCH.CN";
	u8 ids[2];

	buf[0] = 0; // dev_id
	buf[1] = 0; // dev_type
	/* do not send the terminating nul byte, hence sizeof(buf) - 1 */
	isp_send_cmd(CMD_IDENTIFY, sizeof(buf) - 1, buf);
	isp_recv_cmd(CMD_IDENTIFY, sizeof(ids), ids);

	if (dev_id)
		*dev_id = ids[0];
	if (dev_type)
		*dev_type = ids[1];
}

static void
cmd_isp_key(size_t len, u8 *key, u8 *sum)
{
	u8 rsp[2];

	isp_send_cmd(CMD_ISP_KEY, len, key);
	isp_recv_cmd(CMD_ISP_KEY, sizeof(rsp), rsp);
	if (sum)
		*sum = rsp[0];
}

static void
cmd_isp_end(u8 reason)
{
	u8 buf[2];

	isp_send_cmd(CMD_ISP_END, sizeof(reason), &reason);
	if (reason == 0) /* the device is not expected to resond */
		isp_recv_cmd(CMD_ISP_END, sizeof(buf), buf);
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
		die("Fail to verify chunk @ %#x error: %.2x %.2x\n", addr, rsp[0], rsp[1]);

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

static void
usb_init(void)
{
	int err;

	err = libusb_init(&usb);
	if (err)
		die("libusb_init: %s\n", libusb_strerror(err));

	dev = libusb_open_device_with_vid_pid(usb, ISP_VID, ISP_PID);
	if (!dev)
		die("Fail to open device %4x:%4x %s\n", ISP_VID, ISP_PID,
		    strerror(errno));

	kernel = libusb_kernel_driver_active(dev, 0);
	if (kernel)
		if (libusb_detach_kernel_driver(dev, 0))
			die("Couldn't detach kernel driver!\n");

	err = libusb_claim_interface(dev, 0);
	if (err)
		die("libusb_claim_interface: %s\n", libusb_strerror(err));
}

static void
usb_fini(void)
{
	int err = 0;

	if (dev)
		err = libusb_release_interface(dev, 0);
	if (err)
		die("libusb_release_interface: %s\n", libusb_strerror(err));

	if (kernel)
		libusb_attach_kernel_driver(dev, 0);

	if (usb)
		libusb_exit(usb);
}

static u8 dev_id;
static u8 dev_type;
static u8 dev_uid[8];
static size_t dev_uid_len;
static u8 isp_key[30]; /* all zero key */
static u8 xor_key[8];

static struct dev *dev_db;

static int do_progress;
static int do_reset;
static int do_verify;

static void
isp_init(void)
{
	size_t i;
	u8 sum;
	u8 rsp;

	/* get the device type and id */
	cmd_identify(&dev_id, &dev_type);

	/* match the detected device */
	for (i = 0; i < LEN(devices); i++) {
		if (devices[i].type == dev_type && devices[i].id == dev_id) {
			dev_db = &devices[i];
			break;
		}
	}
	if (dev_db) {
		printf("Found chip: %s [0x%.2x%.2x] Flash %dK\n", dev_db->name,
		       dev_type, dev_id, dev_db->flash_size / SZ_1K);
	} else {
		printf("Unknown chip: [0x%.2x%.2x]", dev_type, dev_id);
	}

	/* get the device uid */
	dev_uid_len = cmd_read_conf(CFG_MASK_UID, sizeof(dev_uid), dev_uid);
	printf("chip uid: ");
	for (i = 0; i < dev_uid_len; i++)
		printf("%.2x ", dev_uid[i]);
	puts("");

	/* initialize xor_key */
	for (sum = 0, i = 0; i < dev_uid_len; i++)
		sum += dev_uid[i];
	memset(xor_key, sum, sizeof(xor_key));
	xor_key[7] = xor_key[0] + dev_id;

	/* send the isp key, the reply has a check sum of the xor_key used */
	cmd_isp_key(sizeof(isp_key), isp_key, &rsp);

	/* do the same on our side */
	for (sum = 0, i = 0; i < sizeof(xor_key); i++)
		sum += xor_key[i];

	/* verify that we both are using the same xor_key (thanks to the checksum) */
	if (rsp != sum)
		die("failed set isp key, wrong checksum, got %x (exp %x)\n", rsp, sum);
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
	printf("\r[%.*s%.*s] %s %ld/%ld", n, f, l - n, e, act, current, total);
	if (current == total)
		puts("");
	fflush(stdout);
}

static void
isp_flash(size_t size, u8 *data)
{
	u16 nr_sectors = (size / SECTOR_SIZE) + 1;
	size_t off = 0;
	size_t rem = size;
	size_t len;

	nr_sectors = MIN(nr_sectors, 8);
	cmd_erase(nr_sectors);

	while (off < size) {
		progress_bar("write", off, size);

		len = cmd_program(off, rem, data + off, xor_key);
		off += len;
		rem -= len;
	}
	cmd_program(off, 0, NULL, xor_key);
	progress_bar("write", size, size);
}

static void
isp_verify(size_t size, u8 *data)
{
	size_t off = 0;
	size_t rem = size;
	size_t len;

	while (off < size) {
		progress_bar("verify", off, size);

		len = cmd_verify(off, rem, data + off, xor_key);
		off += len;
		rem -= len;
	}
	progress_bar("verify", size, size);
}

static void
isp_fini(void)
{
	if (do_reset)
		cmd_isp_end(1);
}

static void
flash_file(const char *name)
{
	struct stat sb;
	void *bin;
	int fd;

	fd = open(name, O_RDONLY);
	if (fd == -1)
		die("%s: %s\n", name, strerror(errno));
	if (fstat(fd, &sb) < 0)
		die("fstat: %s\n", strerror(errno));

	bin = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (bin == NULL)
		die("mmap: %s\n", strerror(errno));

	isp_flash(sb.st_size, bin);
	if (do_verify)
		isp_verify(sb.st_size, bin);

	close(fd);
	munmap(bin, sb.st_size);
}

char *argv0;

static void
usage(void)
{
	printf("usage: %s [-Vprv] COMMAND [ARG ...]\n", argv0);
	printf("       %s [-Vprv] flash FILE\n", argv0);
	die("");
}

#define VERSION "0.0.0"

static void
version(void)
{
	printf("%s %s\n", argv0, VERSION);
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
	case 'V':
		version();
	default:
		usage();
	} ARGEND;

	usb_init();
	isp_init();

	if (argc < 1)
		die("missing command\n");
	if (strcmp(argv[0], "flash") == 0) {
		if (argc < 2)
			die("flash: missing file\n");
		flash_file(argv[1]);
	}

	isp_fini();
	usb_fini();

	return 0;
}
