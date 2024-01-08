#ifndef BUILD_SMALL
#include <yaml.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "wch_yaml_parse.h"

#ifdef _MSC_VER //not #if defined(_WIN32) || defined(_WIN64) because we have strncasecmp in mingw
  #define strcasecmp _stricmp
#endif

//debug
#define INDENT "  "
#define STRVAL(x) ((x) ? (char*)(x) : "")

static void indent(int level){
  int i;
  for(i = 0; i < level; i++)printf("%s", INDENT);
}

#define YAMLT_UNKNOWN	0
#define YAMLT_SCALAR	1
#define YAMLT_STRUCT	2
#define YAMLT_ARRAY	3
#define YAMLT_REF	4

//debug
__attribute__((unused)) static char *names[] = {
  "unknown",
  "scalar",
  "struct",
  "array",
  "ref",
};

struct yaml_var;
typedef struct yaml_var yvar_t;
struct yaml_var{
  yvar_t *parent;
  char *name;
  char type;
  union{
    char *value;
    char *ref;
    yvar_t *arr;
  };
  size_t count;
  size_t arr_size;
};

__attribute__((unused)) static void yvar_show(yvar_t *cur, int level, int depth){
  if(cur->type == YAMLT_SCALAR){
    printf("%s\n", cur->value);
  }else if(cur->type == YAMLT_REF){
    printf("-> %s\n", cur->ref);
  }else if(cur->type == YAMLT_ARRAY){
    if(depth == 0){printf("[...]\n"); return;}
    printf("[\n");
    for(int i=0; i<cur->count; i++){
      indent(level);
      yvar_show(&cur->arr[i], level+1, depth-1);
    }
    indent(level-1); printf("]\n");
  }else if(cur->type == YAMLT_STRUCT){
    if(depth == 0){printf("{...}\n"); return;}
    printf("{\n");
    for(int i=0; i<cur->count; i++){
      indent(level);
      printf("%s: ", cur->arr[i].name);
      yvar_show(&cur->arr[i], level+1, depth-1);
    }
    indent(level-1); printf("}\n");
  }
}

static yvar_t* yvar_find(yvar_t *root, char *fieldname, char *value){
  if(root == NULL)return NULL;
  if(root->type != YAMLT_STRUCT && root->type != YAMLT_ARRAY)return NULL;
  for(int i=0; i<root->count; i++){
    yvar_t *cur = &root->arr[i];
    if(cur->name && (strcasecmp(cur->name, fieldname)==0)){
      if(value == NULL)return cur;
      if((cur->type == YAMLT_SCALAR || cur->type == YAMLT_REF) && strcasecmp(cur->value, value)==0)return cur;
    }
    if(cur->type == YAMLT_STRUCT || cur->type == YAMLT_ARRAY){
      yvar_t *res = yvar_find(cur, fieldname, value);
      if(res)return res;
    }
  }
  return NULL;
}

static yvar_t* yvar_byname(yvar_t *root, char name[]){
  if(root == NULL)return NULL;
  if(root->type != YAMLT_STRUCT && root->type != YAMLT_ARRAY)return NULL;
  for(int i=0; i<root->count; i++){
    yvar_t *cur = &root->arr[i];
    if(cur->name && (strcasecmp(cur->name, name)==0))return cur;
  }
  return NULL;
}

static void yvar_free(yvar_t *cur){
  for(int i=0; i<cur->count; i++){
    yvar_free(&cur->arr[i]);
  }
  if(cur->arr)free(cur->arr);
  if(cur->name)free(cur->name);
  cur->arr = NULL;
  cur->count = 0;
  cur->type = YAMLT_UNKNOWN;
  cur->name = NULL;
}

static void yvar_init(yvar_t *cur){
  cur->name = NULL;
  cur->arr = NULL;
  cur->type = YAMLT_UNKNOWN;
  cur->count = 0;
  cur->arr_size = 0;
}

static char yvar_reaisze(yvar_t *cur){
  if((cur->count+1) < cur->arr_size)return 1;
  size_t newsize = cur->arr_size + 8;
  yvar_t *temp = realloc(cur->arr, sizeof(yvar_t)*newsize);
  if(temp == NULL){
    fprintf(stderr, "yvar_reaisze: can not allocate memory\n");
    return 0;
  }
  cur->arr = temp;
  cur->arr_size = newsize;
  for(int i=0; i<cur->count; i++){
    temp = &(cur->arr[i]);
    temp->parent = cur;
    if(temp->type != YAMLT_STRUCT && temp->type != YAMLT_ARRAY)continue;
    for(int j=0; j<temp->count; j++){
      temp->arr[j].parent = temp;
    }
  }
  return 1;
}

static char yvar_parse(yvar_t *cur, yaml_parser_t *parser){
  yaml_event_t event;
  yaml_event_type_t event_type;
  
  cur->arr_size = 0;
  cur->count = 0;
  cur->arr = NULL;
  if(!yvar_reaisze(cur))return 0;
  cur->count = 1;
  yvar_t *var = &cur->arr[0];
  yvar_init(var);
  var->parent = cur;
  
  do{
    if(!yaml_parser_parse(parser, &event))break;
    event_type = event.type;
    
    switch(event_type){
      case YAML_MAPPING_START_EVENT:
        var->type = YAMLT_STRUCT;
        if(!yvar_parse(var, parser))return 0;
        if(!yvar_reaisze(cur))return 0;
        cur->count++;
        var = &cur->arr[cur->count-1];
        yvar_init(var);
        var->parent = cur;
        break;
      case YAML_MAPPING_END_EVENT:
        yaml_event_delete(&event);
        cur->count--;
        return 1;
      case YAML_SEQUENCE_START_EVENT:
        var->type = YAMLT_ARRAY;
        if(!yvar_parse(var, parser))return 0;
        if(!yvar_reaisze(cur))return 0;
        cur->count++;
        var = &cur->arr[cur->count-1];
        yvar_init(var);
        var->parent = cur;
        break;
      case YAML_SEQUENCE_END_EVENT:
        yaml_event_delete(&event);
        cur->count--;
        return 1;
      case YAML_SCALAR_EVENT:
          if((var->name == NULL) && (cur->type != YAMLT_ARRAY)){
            var->name = strdup((char*)event.data.scalar.value);
          }else{
            var->type = YAMLT_SCALAR;
            var->value = strdup((char*)event.data.scalar.value);
            if(!yvar_reaisze(cur))return 0;
            cur->count++;
            var = &cur->arr[cur->count-1];
            yvar_init(var);
            var->parent = cur;
          }
        break;
      case YAML_ALIAS_EVENT:
        var->type = YAMLT_REF;
        var->ref = strdup((char*)event.data.alias.anchor);
        if(!yvar_reaisze(cur))return 0;
        cur->count++;
        var = &cur->arr[cur->count-1];
        yvar_init(var);
        var->parent = cur;
        break;
      case YAML_NO_EVENT: case YAML_STREAM_START_EVENT: case YAML_STREAM_END_EVENT:
      case YAML_DOCUMENT_START_EVENT: case YAML_DOCUMENT_END_EVENT:
        break;
    }
    
    yaml_event_delete(&event);
  }while (event_type != YAML_STREAM_END_EVENT);
  if(cur->type == YAMLT_ARRAY || cur->type == YAMLT_STRUCT)cur->count--;
  return 1;
}

static yvar_t* yvar_load(char filename[]){
  FILE *pf = fopen(filename, "rt");
  yvar_t *var = (yvar_t*)malloc(sizeof(yvar_t));
  yvar_init(var);
  var->type = YAMLT_STRUCT;
  
  yaml_parser_t parser;
  
  yaml_parser_initialize(&parser);
  yaml_parser_set_input_file(&parser, pf);
  
  char res = yvar_parse(var, &parser);
  if(!res){
    yaml_parser_delete(&parser);
    yvar_free(var);
    free(var);
    return NULL;
  }
  
  yaml_parser_delete(&parser);
  return var;
}


////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
//       wch_info
////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////


static uint32_t read_bitfield(char *str){
  if(str[0] < '0' || str[0] > '9')return 0xFFFFFFFF;
  if(str[0] == '0'){
    if(str[1] == 'b'){
      return strtoull(str+2, NULL, 2);
    }else if(str[1] == 'x'){
      return strtoull(str+2, NULL, 16);
    }
  }
  return strtoull(str, NULL, 10);
}

static uint32_t read_units(wch_info_t *info, char *str){
  uint32_t res = 0;
  char *uns;
  res = strtoull(str, &uns, 10);
  if(uns[0] == 'k'){
    res *= 1024;
  }else if(uns[0] == 'M'){
    res *= 1024*1024;
  }else if(uns[0] != 0){
    info->errflag = 1;
    fprintf(stderr, "Warning: unknown units [%c] in 'flash_size' field\n", uns[0]);
  }
  return res;
}

wch_info_t* wch_info_read_file(char filename[], uint8_t type, uint8_t id){
  char buf[100];
  yvar_t *root = yvar_load(filename);
  if(root == NULL){fprintf(stderr, "Error opening [%s]\n", filename); return NULL;}
  
  sprintf(buf, "0x%.2X", type);
  yvar_t *com = yvar_find(root, "device_type", buf);
  if(com == NULL){yvar_free(root); free(root); return NULL;}
  
  sprintf(buf, "0x%.2X", id);
  yvar_t *dev = yvar_find(root, "chip_id", buf);
  if(dev == NULL){yvar_free(root); free(root); return NULL;}
  
  
  wch_info_t *info = (wch_info_t*)malloc(sizeof(wch_info_t));
  if(info == NULL){return NULL;}
  info->type = type;
  info->id = id;
  info->reg_count = 0;
  info->reg = NULL;
  info->reg_correct = 0;
  info->errflag = 0;
  
  yvar_t *cur;
  
  // device name
  cur = yvar_byname(dev->parent, "name");
  if(cur == NULL){
    fprintf(stderr, "Unable to read device name\n");
    info->name = strdup("Unknown");
    info->errflag = 1;
  }else{
    info->name = strdup(cur->value);
  }
  
  //flash size
  cur = yvar_byname(dev->parent, "flash_size");
  if(cur == NULL){
    fprintf(stderr, "Unable to read flash size\n");
    info->flash_size = 0;
    info->errflag = 1;
  }else{
    info->flash_size = read_units(info, cur->value);
  }
  
  //eeprom size
  info->eeprom_size = 0;
  info->eeprom_start_addr = 0;
  cur = yvar_byname(dev->parent, "eeprom_size");
  if(cur != NULL){
    info->eeprom_size = read_units(info, cur->value);
  }
  cur = yvar_byname(dev->parent, "eeprom_start_addr");
  if(cur != NULL){
    info->eeprom_start_addr = read_units(info, cur->value);
  }
  
  //regs
  cur = yvar_byname(dev->parent, "config_registers");
  if(cur != NULL){
    if(cur->type == YAMLT_REF){
      cur = yvar_byname(com->parent, cur->value);
    }
  }
  if(cur == NULL){
    cur = yvar_byname(com->parent, "config_registers");
    if(cur != NULL){
      if(cur->type == YAMLT_REF){
        cur = yvar_byname(com->parent, cur->value);
      }
    }
  }
  if(cur == NULL || cur->type != YAMLT_ARRAY){
    fprintf(stderr, "Unable to read OPTION BYTES (field 'config_registers' in database)\n");
    info->reg_count = 0;
    info->reg = NULL;
    info->errflag = 1;
  }else{
    info->reg = (wch_regs_t*)malloc(sizeof(wch_regs_t) * cur->count);
    info->reg_count = cur->count;
    for(int i=0; i<cur->count; i++){
      wch_regs_t *reg = &info->reg[i];
      yvar_t *r = yvar_byname(&cur->arr[i], "name");
      if(r == NULL)reg->name = strdup("Unknown"); else reg->name = strdup(r->value);
      r = yvar_byname(&cur->arr[i], "offset");
      if(r == NULL)reg->offset = 0; else reg->offset = strtoull(r->value, NULL, 16);
      r = yvar_byname(&cur->arr[i], "reset");
      if(r == NULL)reg->defval = 0xFFFFFFFF; else reg->defval = strtoull(r->value, NULL, 16);
      reg->curval = reg->defval;
      r = yvar_byname(&cur->arr[i], "fields");
      if(r == NULL){reg->field_count=0; reg->field = NULL; continue;}
      reg->field_count = r->count;
      reg->field = (wch_bitfield_t*)malloc(sizeof(wch_bitfield_t) * r->count);
      for(int j=0; j<r->count; j++){
        wch_bitfield_t *fld = &reg->field[j];
        yvar_t *f = yvar_byname(&r->arr[j], "name");
        if(f == NULL)fld->name = strdup("Unknown"); else fld->name = strdup(f->value);
        f = yvar_byname(&r->arr[j], "bit_range");
        fld->var_count = 0;
        fld->variant = NULL;
        if(f == NULL){
          fld->st=fld->en = 0xFF;
        }else{
          fld->en = strtoull(f->arr[0].value, NULL, 10);
          fld->st = strtoull(f->arr[1].value, NULL, 10);
          if(fld->st > fld->en){uint8_t temp = fld->st; fld->st = fld->en; fld->en = temp; }
          f = yvar_byname(&r->arr[j], "explaination");
          if(f == NULL)continue;
          fld->var_count = f->count;
          fld->variant = (wch_bitfield_variant_t*)malloc(sizeof(wch_bitfield_variant_t) * f->count);
          for(int k=0; k<f->count; k++){
            fld->variant[k].val = read_bitfield(f->arr[k].name);
            fld->variant[k].name = strdup(f->arr[k].value);
          }
        }
      }
    }
  }
  
  yvar_free(root); free(root);
  return info;
}

void wch_info_free(wch_info_t **info){
  if(*info == NULL)return;
  free((*info)->name);
  for(int i=0; i<(*info)->reg_count; i++){
    wch_regs_t *reg = &(*info)->reg[i];
    free(reg->name);
    for(int j=0; j<reg->field_count; j++){
      wch_bitfield_t *fld = &reg->field[j];
      free(fld->name);
      for(int k=0; k<(*info)->reg[i].field[j].var_count; k++){
        free(fld->variant[k].name);
      }
      if(fld->variant)free(fld->variant);
    }
    if(reg->field)free(reg->field);
  }
  free((*info)->reg);
  free(*info);
  *info = NULL;
}

static char* show_bin(uint32_t x){
  char uns = ' ';
  static char buf[10];
  if(x < 1024){
    uns = ' ';
  }else if(x < (1024*1024)){
    x /= 1024; uns='k';
  }else if(x < (1024*1024*1024)){
    x /= (1024*1024); uns='M';
  }else{
    strcpy(buf, "(err)>1G"); return buf;
  }
  sprintf(buf, "%u%c", (unsigned int)x, uns);
  return buf;
}

uint32_t wch_bitfield_val(wch_bitfield_t *fld, uint32_t reg){
  if(fld == NULL){fprintf(stderr, "wch_bitfield_val: fld = NULL\n"); return reg;}
  uint32_t mask = 0xFFFFFFFF;
  mask = (mask << (31 - fld->en)) >> (31 - fld->en) >> (fld->st);
  return (reg >> (fld->st)) & mask;
}

static char* wch_bitfield_name(wch_bitfield_t *fld, uint32_t val){
  static char unk[] = "";
  if(fld == NULL){fprintf(stderr, "wch_bitfield_name: fld = NULL\n"); return unk;}
  for(int i=0; i<fld->var_count; i++){
    if(val == fld->variant[i].val)return fld->variant[i].name;
  }
  return unk;
}

static uint32_t bitfield_apply(wch_info_t *info, wch_bitfield_t *fld, uint32_t reg, uint32_t val){
  if(info == NULL){fprintf(stderr, "bitfield_apply: info = NULL\n"); return reg;}
  if(fld == NULL){fprintf(stderr, "bitfield_apply: fld = NULL\n"); return reg;}
  uint32_t mask = 0xFFFFFFFF;
  mask = (mask << (31 - fld->en)) >> (31 - fld->en) >> fld->st;
  uint32_t nval = val & mask;
  mask <<= fld->st;
  uint32_t res = reg &~ mask;
  if(nval != val){
    info->errflag = 1;
    printf("Warning: field '%s' truncated: %X -> %X\n", fld->name, val, nval);
  }
  nval <<= fld->st;
  res |= nval;
  return res;
}


void wch_info_regs_import(wch_info_t *info, uint32_t *regs, size_t reg_count){
  if(info == NULL){fprintf(stderr, "wch_info_regs_import: info = NULL\n"); return;}
  if(info->reg == NULL){fprintf(stderr, "wch_info_regs_import: info.regs = NULL\n"); return;}
  if(reg_count > info->reg_count)reg_count = info->reg_count;
  for(int i=0; i<reg_count; i++){
    size_t idx = info->reg[i].offset / 4;
    info->reg[i].curval = regs[idx];
  }
  info->reg_correct = 1;
}

void wch_info_regs_export(wch_info_t *info, uint32_t *regs, size_t reg_count){
  if(info == NULL){fprintf(stderr, "wch_info_regs_import: info = NULL\n"); return;}
  if(info->reg == NULL){fprintf(stderr, "wch_info_regs_import: info.regs = NULL\n"); return;}
  if(reg_count > info->reg_count)reg_count = info->reg_count;
  if(info->reg_correct){
    for(int i=0; i<reg_count; i++){
      size_t idx = info->reg[i].offset / 4;
      regs[idx] = info->reg[i].curval;
    }
  }else{
    for(int i=0; i<reg_count; i++){
      size_t idx = info->reg[i].offset / 4;
      regs[idx] = info->reg[i].defval;
    }
  }
}

static wch_regs_t* regs_byname(wch_info_t *info, char name[]){
  if(info == NULL){fprintf(stderr, "regs_byname: info = NULL\n"); return NULL;}
  if(info->reg == NULL){fprintf(stderr, "regs_byname: info.regs = NULL\n"); return NULL;}
  for(int i=0; i<info->reg_count; i++){
    if(strcasecmp(info->reg[i].name, name)==0)return &info->reg[i];
  }
  return NULL;
}

wch_regs_t* wch_bitfield_byname(wch_info_t *info, char name[], wch_bitfield_t **res_field){
  *res_field = NULL;
  if(info == NULL){fprintf(stderr, "wch_bitfield_byname: info = NULL\n"); return NULL;}
  if(info->reg == NULL){fprintf(stderr, "wch_bitfield_byname: info.regs = NULL\n"); return NULL;}
  for(int i=0; i<info->reg_count; i++){
    for(int j=0; j<info->reg[i].field_count; j++){
      if(strcasecmp(info->reg[i].field[j].name, name)==0){
        *res_field = &(info->reg[i].field[j]);
        return &info->reg[i];
      }
    }
  }
  return NULL;
}

void wch_info_show(wch_info_t *info){
  if(info == NULL){fprintf(stderr, "wch_info_show: info = NULL\n"); return;}
  printf("Device 0x%.2X 0x%.2X\n", info->type, info->id);
  printf("name = [%s]\n", info->name);
  printf("flash size = %s\n", show_bin(info->flash_size));
  if(info->eeprom_size){
    printf("eeprom %s, ", show_bin(info->eeprom_size));
    printf("starts from %s\n", show_bin(info->eeprom_start_addr));
  }
  if(info->reg_count == 0){
    printf("Warning: unable to read Option bytes\n");
    info->errflag = 1;
    return;
  }
  printf("Option bytes:\n");
  for(int i=0; i<info->reg_count; i++){
    printf("  0x%.2X %s ; default = 0x%.8X", info->reg[i].offset, info->reg[i].name, info->reg[i].defval);
    if(info->reg_correct){
      printf(", current = 0x%.8X\n", info->reg[i].curval);
    }else{
      printf("\n");
    }
    for(int j=0; j<info->reg[i].field_count; j++){
      wch_bitfield_t *fld = &(info->reg[i].field[j]);
      if(fld->en == fld->st){
        printf("    [%i] : %s", fld->st, fld->name);
      }else{
        printf("    [%i:%i] : %s", fld->en, fld->st, fld->name);
      }
      
      if(!info->reg_correct){
        uint32_t def = wch_bitfield_val(fld, info->reg[i].defval);
        char *name = wch_bitfield_name(fld, def);
        printf(" = %X => \"%s\"\n", def, name);
      }else{
        uint32_t def = wch_bitfield_val(fld, info->reg[i].defval);
        uint32_t val = wch_bitfield_val(fld, info->reg[i].curval);
        char *name = wch_bitfield_name(fld, val);
        if(def == val){
          printf(" = %X => \"%s\"\n", def, name);
        }else{
          printf(" = %X (def = %X) => \"%s\"\n", val, def, name);
        }
      }

      for(int k=0; k<fld->var_count; k++){
        if(fld->variant[k].val <= 9){
          printf("      %X : %s\n", fld->variant[k].val, fld->variant[k].name);
        }else{
          printf("      0x%X : %s\n", fld->variant[k].val, fld->variant[k].name);
        }
      }
    }
  }
}

char wch_info_modify(wch_info_t *info, char str[]){
  if(info == NULL){fprintf(stderr, "wch_info_modify: info = NULL\n"); info->errflag = 1; return 0;}
  if(info->reg == NULL){fprintf(stderr, "wch_info_modify: info.regs = NULL\n"); info->errflag = 1; return 0;}
  if(!(info->reg_correct)){fprintf(stderr, "wch_info_modify: no data read from MCU\n"); info->errflag = 1; return 0;}
  const char delim[] = " \t\n,;";
  char *buf = strdup(str);
  char *tok = strtok(buf, delim);
  int N = 0;
  do{
    N++;
    char *val = NULL, *ch;
    ch = strchr(tok, '=');
    if(ch != NULL){
      val = ch+1;
      ch[0] = 0;
      if(val[0] == 0 || strchr(delim, val[0])!=NULL ){
        val = strtok(NULL, delim);
      }
    }else{
      val = strtok(NULL, delim);
      if(val == NULL){
        fprintf(stderr, "Option bytes modify: wrong string\n");
        info->errflag = 1;
        free(buf);
        return 0;
      }
      if(val[0] == '='){
        if(val[1] == 0){
          val = strtok(NULL, delim);
        }else{
          val++;
        }
      }
    }
    if(val == NULL){fprintf(stderr, "Option bytes modify: wrong string\n"); free(buf); info->errflag = 1; return 0;}
    
    uint32_t ival;
    if(sscanf(val, "%i", &ival) < 1){
      fprintf(stderr, "Option bytes modify: wrong string\n");
      free(buf);
      info->errflag = 1;
      return 0;
    }
    
    wch_regs_t *reg = regs_byname(info, tok);
    if(reg != NULL){
      reg->curval = ival;
    }else{
      wch_bitfield_t *fld = NULL;
      reg = wch_bitfield_byname(info, tok, &fld);
      if(fld == NULL){
        fprintf(stderr, "Incorrect name [%s]\n", tok); free(buf); info->errflag = 1; return 0;
      }
      reg->curval = bitfield_apply(info, fld, reg->curval, ival);
    }
    
  }while((tok = strtok(NULL, delim)) );
  
  free(buf);
  return 1;
}


#if defined(linux) || defined(__linux) || defined(__linux__) || defined(__GNU__) || defined(__GLIBC__)\
    || defined(__APPLE__) || defined(__MACH__)
    
#include <dirent.h>
wch_info_t* wch_info_read_dir(char dirname[], char recur, uint8_t type, uint8_t id){
  wch_info_t *res = NULL;
  DIR *dir;
  struct dirent *entry;
  size_t rootlen = strlen(dirname);

  dir = opendir(dirname);
  if(!dir){fprintf(stderr, "Unable to open directory [%s]\n", dirname); return NULL;}

  while( (entry = readdir(dir)) != NULL){
    if(entry->d_type == DT_DIR){
      if(!recur)continue;
      if(entry->d_name[0] == '.')continue;
      size_t leaflen = strlen(entry->d_name);
      char *newname = malloc(rootlen + leaflen + 2);
      memcpy(newname, dirname, rootlen);
      newname[rootlen] = '/';
      memcpy(&newname[rootlen+1], entry->d_name, leaflen);
      newname[rootlen + 1 + leaflen] = 0;
      res = wch_info_read_dir(newname, recur, type, id);
      free(newname);
      if(res){
        closedir(dir);
        return res;
      }
    }else if(entry->d_type == DT_REG){
      size_t leaflen = strlen(entry->d_name);
      if(leaflen < 5)continue;
      if(strcasecmp(&(entry->d_name[leaflen-5]), ".yaml") != 0)continue;
      char *newname = malloc(rootlen + leaflen + 2);
      memcpy(newname, dirname, rootlen);
      newname[rootlen] = '/';
      memcpy(&newname[rootlen+1], entry->d_name, leaflen);
      newname[rootlen + 1 + leaflen] = 0;
      res = wch_info_read_file(newname, type, id);
      free(newname);
      if(res){
        closedir(dir);
        return res;
      }
    }
  };

  closedir(dir);
  return NULL;
}

#elif defined(_WIN32) || defined(__WIN32__) || defined(WIN32)

#include <windows.h>
wch_info_t* wch_info_read_dir(char dirname[], char recur, uint8_t type, uint8_t id){
  wch_info_t *res = NULL;
  size_t rootlen = strlen(dirname);
  char *path = malloc(rootlen + 5);
  memcpy(path, dirname, rootlen);
  path[rootlen] = '/';
  path[rootlen+1] = '*';
  path[rootlen+2] = 0;
  WIN32_FIND_DATA ffd;
  HANDLE hFind = FindFirstFile(path, &ffd);
  do{
    if(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
      if(!recur)continue;
      if(ffd.cFileName[0] == '.')continue;
      size_t leaflen = strlen(ffd.cFileName);
      char *newname = malloc(rootlen + leaflen + 5);
      memcpy(newname, dirname, rootlen);
      newname[rootlen] = '/';
      memcpy(&newname[rootlen+1], ffd.cFileName, leaflen);
      newname[rootlen + 1 + leaflen] = 0;
      res = wch_info_read_dir(newname, recur, type, id);
      free(newname);
      if(res){
        FindClose(hFind);
        free(path);
        return res;
      }
    }else{
      size_t leaflen = strlen(ffd.cFileName);
      if(leaflen < 5)continue;
      if(strcasecmp(&(ffd.cFileName[leaflen-5]), ".yaml") != 0)continue;
      char *newname = malloc(rootlen + leaflen + 2);
      memcpy(newname, dirname, rootlen);
      newname[rootlen] = '/';
      memcpy(&newname[rootlen+1], ffd.cFileName, leaflen);
      newname[rootlen + 1 + leaflen] = 0;
      res = wch_info_read_file(newname, type, id);
      free(newname);
      if(res){
        FindClose(hFind);
        free(path);
        return res;
      }
    }
  }while(FindNextFile(hFind, &ffd) != 0);

  FindClose(hFind);
  free(path);        
  return NULL;
}

#else
  #error "Unsupported platform"
#endif

#else //ifdef BUILD_SMALL

#warning DATABASE DISABLED
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "wch_yaml_parse.h"
wch_info_t* wch_info_read_file(char filename[], uint8_t type, uint8_t id){return NULL;}
wch_info_t* wch_info_read_dir(char dirname[], char recur, uint8_t type, uint8_t id){return NULL;}
void wch_info_free(wch_info_t **info){}
void wch_info_show(wch_info_t *info){}
void wch_info_regs_import(wch_info_t *info, uint32_t *regs, size_t reg_count){}
void wch_info_regs_export(wch_info_t *info, uint32_t *regs, size_t reg_count){}
char wch_info_modify(wch_info_t *info, char str[]){return 0;}
wch_regs_t* wch_bitfield_byname(wch_info_t *info, char name[], wch_bitfield_t **res_field){return NULL;}
uint32_t wch_bitfield_val(wch_bitfield_t *fld, uint32_t reg){return 0;}


#endif
