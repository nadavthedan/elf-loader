#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  Elf64_Ehdr elf_header;
  Elf64_Phdr *program_headers;
  Elf64_Shdr *section_headers;
} Elf64_Headers;

int read_elf_header(FILE *fp, Elf64_Headers *elf) {
  if (!fp) {
    printf("ERROR: Received File Pointer is NULL\n");
    return -1;
  }

  rewind(fp);

  unsigned char ident[EI_NIDENT];
  if (fread(ident, 1, EI_NIDENT, fp) != EI_NIDENT) {
    printf("ERROR: Failed to read file idnetifier\n");
    return -1;
  }

  if (ident[EI_MAG0] != ELFMAG0 || ident[EI_MAG1] != ELFMAG1 ||
      ident[EI_MAG2] != ELFMAG2 || ident[EI_MAG3] != ELFMAG3) {
    printf("ERROR: Unexpected magic number for elf file\n");
    return -1;
  }

  rewind(fp);

  if (ident[EI_CLASS] != ELFCLASS64) {
    printf("ERROR: Unsupported architechture\n");
    return -1;
  }

  fread(&elf->elf_header, sizeof(Elf64_Ehdr), 1, fp);

  return 0;
}

int read_elf_program_headers(FILE *fp, Elf64_Headers *elf) {
  if (!fp) {
    printf("ERROR: Received File Pointer is NULL\n");
    return -1;
  }

  Elf64_Phdr *program_headers =
      malloc(elf->elf_header.e_phentsize * elf->elf_header.e_phnum);
  if (program_headers == NULL) {
    printf("ERROR: failed to malloc space for program_headers\n");
    return -1;
  }
  elf->program_headers = program_headers;
  fseek(fp, elf->elf_header.e_phoff, SEEK_SET);
  fread(elf->program_headers, sizeof(Elf64_Phdr), elf->elf_header.e_phnum, fp);

  return 0;
}

int read_elf_section_headers(FILE *fp, Elf64_Headers *elf) {
  if (!fp) {
    printf("ERROR: Received File Pointer is NULL\n");
    return -1;
  }

  Elf64_Shdr *section_headers =
      malloc(elf->elf_header.e_shentsize * elf->elf_header.e_shnum);
  if (section_headers == NULL) {
    printf("ERROR: failed to malloc space for section_headers\n");
    return -1;
  }
  elf->section_headers = section_headers;
  fseek(fp, elf->elf_header.e_shoff, SEEK_SET);
  fread(elf->section_headers, sizeof(Elf64_Shdr), elf->elf_header.e_shnum, fp);

  return 0;
}
