#include <elf.h>
#include <endian.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  Elf64_Ehdr elf_header;
  Elf64_Phdr *program_headers;
  Elf64_Shdr *section_headers;
} Elf64_Headers;

char elf_header_validate(FILE *fp) {
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

int elf_headers_read(FILE *fp, Elf64_Headers *elf) {
  char valid;
  if (!fp) {
    printf("ERROR: Received File Pointer is NULL\n");
    return -1;
  }

  valid = elf_header_validate(fp);
  if (!valid) {
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

  return 0;
}
