#include "elfutils.h"
#include <elf.h>
#include <endian.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

int elf_header_validate(FILE *fp) {
  rewind(fp);
  unsigned char ident[EI_NIDENT];
  if (fread(ident, 1, EI_NIDENT, fp) != EI_NIDENT) {
    printf("ERROR: Failed to read file idnetifier\n");
    return 0;
  }

  if (ident[EI_MAG0] != ELFMAG0 || ident[EI_MAG1] != ELFMAG1 ||
      ident[EI_MAG2] != ELFMAG2 || ident[EI_MAG3] != ELFMAG3) {
    printf("ERROR: Unexpected magic number for elf file\n");
    return 0;
  }

  if (ident[EI_CLASS] != ELFCLASS64) {
    printf("ERROR: Unsupported architechture\n");
    return 0;
  }

  if (ident[EI_VERSION] != EV_CURRENT) {
    printf("ERROR: Unsupported elf version\n");
    return 0;
  }

  return 1;
}

int elf_headers_read(FILE *fp, Elf64_Data *elf) {
  int ret;
  ulong i;
  Elf64_Shdr shdr;
  if (!fp) {
    printf("ERROR: Received File Pointer is NULL\n");
    return -1;
  }

  ret = elf_header_validate(fp);
  if (!ret) {
    printf("ERROR: Invalid elf header\n");
    return -1;
  }

  // Read Elf Header
  rewind(fp);
  fread(&elf->elf_header, sizeof(Elf64_Ehdr), 1, fp);

  // Read Elf Program Headers
  elf->program_headers =
      malloc(elf->elf_header.e_phentsize * elf->elf_header.e_phnum);
  if (elf->program_headers == NULL) {
    printf("ERROR: failed to malloc space for program_headers\n");
    return -1;
  }
  fseek(fp, elf->elf_header.e_phoff, SEEK_SET);
  fread(elf->program_headers, sizeof(Elf64_Phdr), elf->elf_header.e_phnum, fp);

  // Read Elf Section Headers
  elf->section_headers =
      malloc(elf->elf_header.e_shentsize * elf->elf_header.e_shnum);
  if (elf->section_headers == NULL) {
    printf("ERROR: failed to malloc space for section_headers\n");
    return -1;
  }
  fseek(fp, elf->elf_header.e_shoff, SEEK_SET);
  fread(elf->section_headers, sizeof(Elf64_Shdr), elf->elf_header.e_shnum, fp);

  if (elf->elf_header.e_shstrndx != SHN_UNDEF) {
    // Read String Section Headers Table
    ret = populate_str_table(elf, &elf->string_section_headers_table,
                             elf->elf_header.e_shstrndx, fp);
    if (ret != 0) {
      printf("ERROR: falied to populate str section headers table\n");
      return -1;
    }
  }

  for (i = 0; i < elf->elf_header.e_shnum; i++) {
    shdr = elf->section_headers[i];
    switch (shdr.sh_type) {
    case SHT_DYNAMIC:
      elf->dynamics_len = shdr.sh_size / sizeof(Elf64_Dyn);
      read_section((void **)&elf->dynamics, sizeof(Elf64_Dyn), shdr, fp);
      break;
    case SHT_SYMTAB:
      ret = populate_str_table(elf, &elf->string_table, shdr.sh_link, fp);
      if (ret != 0) {
        printf("ERROR: falied to populate str table\n");
        return -1;
      }
      elf->symbols_len = shdr.sh_size / sizeof(Elf64_Sym);
      read_section((void **)&elf->symbols, sizeof(Elf64_Sym), shdr, fp);
      break;
    case SHT_DYNSYM:
      ret = populate_str_table(elf, &elf->string_dyn_table, shdr.sh_link, fp);
      if (ret != 0) {
        printf("ERROR: falied to populate str table\n");
        return -1;
      }
      elf->symbols_dyn_len = shdr.sh_size / sizeof(Elf64_Sym);
      read_section((void **)&elf->symbols_dyn, sizeof(Elf64_Sym), shdr, fp);
      break;
    case SHT_REL:
      elf->rels_len = shdr.sh_size / sizeof(Elf64_Rel);
      read_section((void **)&elf->rels, sizeof(Elf64_Rel), shdr, fp);
      break;
    case SHT_RELA:
      elf->relas_len = shdr.sh_size / sizeof(Elf64_Rela);
      read_section((void **)&elf->relas, sizeof(Elf64_Rela), shdr, fp);
      break;
    }
  }

  return 0;
}

int populate_str_table(Elf64_Data *elf, char **str_table, uint shidx,
                       FILE *fp) {
  Elf64_Shdr shdr;
  shdr = elf->section_headers[shidx];
  *str_table = malloc(shdr.sh_size);
  if (*str_table == NULL) {
    printf("ERROR: failed to malloc str_table\n");
    return -1;
  }
  fseek(fp, shdr.sh_offset, SEEK_SET);
  fread(*str_table, sizeof(char), shdr.sh_size, fp);

  return 0;
}

int read_section(void **section, size_t size, Elf64_Shdr shdr, FILE *fp) {
  section = malloc(shdr.sh_size);
  uint16_t len = shdr.sh_size / size;
  if (section == NULL) {
    printf("ERROR: failed to malloc space\n");
    return 1;
  }
  fseek(fp, shdr.sh_offset, SEEK_SET);
  fread(section, size, len, fp);
  return 0;
}
