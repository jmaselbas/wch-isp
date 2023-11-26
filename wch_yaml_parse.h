#ifndef __WCH_YAML_PARSE_H__
#define __WCH_YAML_PARSE_H__

typedef struct{
  uint32_t val;
  char *name;
}wch_bitfield_variant_t;

typedef struct{
  uint8_t st, en;
  char *name;
  char *descr;
  size_t var_count;
  wch_bitfield_variant_t *variant;
}wch_bitfield_t;

typedef struct{
  uint8_t offset;
  char *name;
  uint32_t defval;
  uint32_t curval;
  size_t field_count;
  wch_bitfield_t *field;
}wch_regs_t;

typedef struct{
  uint8_t type, id;
  char *name;
  size_t flash_size;
  size_t eeprom_size;
  size_t eeprom_start_addr;
  size_t reg_count;
  wch_regs_t *reg;
  char reg_correct;
  char errflag;
}wch_info_t;


wch_info_t* wch_info_read_file(char filename[], uint8_t type, uint8_t id);
wch_info_t* wch_info_read_dir(char dirname[], char recur, uint8_t type, uint8_t id);
void wch_info_free(wch_info_t **info);
void wch_info_show(wch_info_t *info);
void wch_info_regs_import(wch_info_t *info, uint32_t *regs, size_t reg_count);
void wch_info_regs_export(wch_info_t *info, uint32_t *regs, size_t reg_count);
char wch_info_modify(wch_info_t *info, char str[]);

#endif