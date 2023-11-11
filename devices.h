#define SZ_1K  (1024)
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

/* record for a device */
struct db_dev {
	u8 id;
	const char *name;
	u32 flash_size;
	u32 eeprom_size;
};

/* record for a device family */
struct db {
	u8 type;
	u32 flash_sector_size;
	const char *name;
	int (*show_conf)(struct isp_dev *dev, size_t len, u8 *cfg);
	const struct db_dev *devs;
};

const struct db devices[] = {
	{ .type = 0x10, .flash_sector_size = 256,
	  .name = "CH56x",
	  .show_conf = ch56x_show_conf,
	  .devs = (const struct db_dev[]){
		{ 0x63, "CH563", SZ_224K, SZ_28K, },
		{ 0x42, "CH563", SZ_224K, SZ_28K, },
		{ 0x43, "CH563", SZ_224K, SZ_28K, },
		{ 0x44, "CH563", SZ_224K, SZ_28K, },
		{ 0x45, "CH563", SZ_224K, SZ_28K, },
		{ 0x65, "CH565", SZ_448K, SZ_32K, },
		{ 0x66, "CH566",  SZ_64K, SZ_32K, },
		{ 0x67, "CH567", SZ_192K, SZ_32K, },
		{ 0x68, "CH568", SZ_192K, SZ_32K, },
		{ 0x69, "CH569", SZ_448K, SZ_32K, },
		{ /* sentinel */ } }
	},
	{ .type = 0x11, .flash_sector_size = 1024,
	  .name = "CH55x",
	  .devs = (const struct db_dev[]){
		{ 0x51, "CH551", SZ_10K,  128 },
		{ 0x52, "CH552", SZ_14K,  128 },
		{ 0x53, "CH554", SZ_14K,  128 },
		{ 0x55, "CH555", SZ_60K,  SZ_1K },
		{ 0x56, "CH556", SZ_60K,  SZ_1K },
		{ 0x57, "CH557", SZ_60K,  SZ_1K },
		{ 0x58, "CH558", SZ_32K,  SZ_5K },
		{ 0x59, "CH559", SZ_60K,  SZ_1K },
		{ /* sentinel */ } }
	},
	{ .type = 0x14, .flash_sector_size = 1024,
	  .name = "CH32F103",
	  .devs = (const struct db_dev[]){
		{ 0x3f, "CH32F103x6x6", SZ_32K },
		{ 0x33, "CH32F103x8x6", SZ_64K },
		{ /* sentinel */ } }
	},
	{ .type = 0x15, .flash_sector_size = 1024,
	  .name = "CH32V103",
	  .devs = (const struct db_dev[]){
		{ 0x3f, "CH32V103x6x6", SZ_32K },
		{ 0x33, "CH32V103x8x6", SZ_64K },
		{ /* sentinel */ } }
	},
	{ .type = 0x19, .flash_sector_size = 4096,
	  .name = "CH32V20x",
	  .devs = (const struct db_dev[]){
/*TODO*/	//{ ?, "CH32V208GB", SZ_480K }, //flash cache + RAM = 128 + 64
            	{ 0x82, "CH32V208CB", SZ_480K }, //flash cache + RAM = 128 + 64
/*TODO*/	{ 0x83, "CH32V208CB", SZ_480K }, //flash cache + RAM = 128 + 64
		{ 0x81, "CH32V208RB", SZ_480K }, //flash cache + RAM = 128 + 64
		{ 0x80, "CH32V208WB", SZ_480K }, //flash cache + RAM = 128 + 64
                { 0x35, "CH32V203F6,G6,K6", SZ_224K }, //flash cache + RAM = 32 + 10
/*TODO*/	//{ ?, "CH32V203F8,G8,K8", SZ_224K }, //flash cache + RAM = 64 + 20
                { 0x32, "CH32V203K8", SZ_64K }, // flash cache + RAM = 64 + 20
                { 0x33, "CH32V203C6", SZ_32K }, //flash cache + RAM = 32 + 10
		{ 0x31, "CH32V203C8T6", SZ_64K }, //flash cache + RAM = 64 + 20
/*TODO*/	{ 0x30, "CH32V203C8U6", SZ_64K }, //flash cache + RAM = 64 + 20
		{ 0x34, "CH32V203RB", SZ_128K }, //flash cache + RAM = [128+64]
		
		{ /* sentinel */ } }
	},
        { .type = 0x17, .flash_sector_size = 4096,
          .name = "CH32V30x",
          .devs = (const struct db_dev[]){
          	{ 0x33, "CH32V303CB", SZ_480K }, //flash cache + RAM = 128+32
                { 0x32, "CH32V303RB", SZ_480K }, //flash cache + RAM = 128+32
                { 0x31, "CH32V303RC", SZ_480K }, //flash cache + RAM = [256+64]
          	{ 0x30, "CH32V303VC", SZ_480K }, //flash cache + RAM = [256+64]
            	{ 0x50, "CH32V305RB", SZ_480K }, //flash cache + RAM = 128+32
/*TODO*/	//{ ?, "CH32V305FB", SZ_480K }, //flash cache + RAM = 128+32
                { 0x71, "CH32V307RC", SZ_480K }, //flash cache + RAM = [256 + 64]
                { 0x73, "CH32V307WC", SZ_480K }, //flash cache + RAM = [256 + 64]
                { 0x70, "CH32V307VC", SZ_480K }, //flash cache + RAM = [256 + 64]
                
                { /* sentinel */ } }
        },
};
