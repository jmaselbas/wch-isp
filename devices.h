#define SZ_1K  (1024 * 1024)
#define SZ_5K  ( 5 * SZ_1K)
#define SZ_8K  ( 8 * SZ_1K)
#define SZ_10K (10 * SZ_1K)
#define SZ_14K (14 * SZ_1K)
#define SZ_16K (16 * SZ_1K)
#define SZ_28K (28 * SZ_1K)
#define SZ_32K (32 * SZ_1K)
#define SZ_60K (60 * SZ_1K)
#define SZ_64K (64 * SZ_1K)
#define SZ_128K (128 * SZ_1K)
#define SZ_192K (192 * SZ_1K)
#define SZ_224K (224 * SZ_1K)
#define SZ_448K (448 * SZ_1K)
#define SZ_480K (480 * SZ_1K)

struct dev {
	u8 type;
	u8 id;
	const char *name;
	u32 flash_size;
	u32 eeprom_size;
};

struct dev devices[] = {
	/* type    id  name           flash    eeprom */
	{ 0x10, 0x63, "CH563",        SZ_224K, SZ_28K },
	{ 0x10, 0x42, "CH563",        SZ_224K, SZ_28K },
	{ 0x10, 0x43, "CH563",        SZ_224K, SZ_28K },
	{ 0x10, 0x44, "CH563",        SZ_224K, SZ_28K },
	{ 0x10, 0x45, "CH563",        SZ_224K, SZ_28K },

	{ 0x10, 0x65, "CH565",        SZ_448K, SZ_32K },
	{ 0x10, 0x66, "CH566",        SZ_64K,  SZ_32K },
	{ 0x10, 0x67, "CH567",        SZ_192K, SZ_32K },
	{ 0x10, 0x68, "CH568",        SZ_192K, SZ_32K },
	{ 0x10, 0x69, "CH569",        SZ_448K, SZ_32K },

	{ 0x11, 0x51, "CH551",        SZ_10K,  128 },
	{ 0x11, 0x52, "CH552",        SZ_14K,  128 },
	{ 0x11, 0x53, "CH554",        SZ_14K,  128 },
	{ 0x11, 0x55, "CH555",        SZ_60K,  SZ_1K },
	{ 0x11, 0x56, "CH556",        SZ_60K,  SZ_1K },
	{ 0x11, 0x57, "CH557",        SZ_60K,  SZ_1K },
	{ 0x11, 0x58, "CH558",        SZ_32K,  SZ_5K },
	{ 0x11, 0x59, "CH559",        SZ_60K,  SZ_1K },

	{ 0x14, 0x3f, "CH32F103x6x6", SZ_32K },
	{ 0x14, 0x33, "CH32F103x8x6", SZ_64K },

	{ 0x15, 0x3f, "CH32V103x6x6", SZ_32K },
	{ 0x15, 0x33, "CH32V103x8x6", SZ_64K },

	{ 0x19, 0x80, "CH32V208WBU6", SZ_480K },
	{ 0x19, 0x81, "CH32V208RBT6", SZ_128K },
	{ 0x19, 0x82, "CH32V208CBU6", SZ_128K },
	{ 0x19, 0x83, "CH32V208CBU6", SZ_128K },
	{ 0x19, 0x30, "CH32V203C8U6", SZ_64K },
	{ 0x19, 0x31, "CH32V203C8T6", SZ_64K },
	{ 0x19, 0x32, "CH32V203K8T6", SZ_64K },
	{ 0x19, 0x33, "CH32V203C6T6", SZ_32K },
	{ 0x19, 0x34, "CH32V203RBT6", SZ_128K },
	{ 0x19, 0x35, "CH32V203K6T6", SZ_32K },
	{ 0x19, 0x35, "CH32V203G6U6", SZ_32K },
	{ 0x19, 0x35, "CH32V203F6P6", SZ_32K },
	{ 0x19, 0x37, "CH32V203F6P6", SZ_32K },
};
