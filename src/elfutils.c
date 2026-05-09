#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  uint8_t bit_class;
  union {
    Elf32_Ehdr h32;
    Elf64_Ehdr h64;
  } elf_header;
  union {
    Elf32_Phdr *h32;
    Elf64_Phdr *h64;
  } program_headers;
  union {
    Elf32_Shdr *h32;
    Elf64_Shdr *h64;
  } section_headers;
} elf_generic_headers;

int read_elf_header(FILE *fp, elf_generic_headers *elf) {
  if (!fp) {
    printf("ERROR: Received File Pointer is NULL\n");
    return -1;
  }

  rewind(fp);

  unsigned char ident[EI_NIDENT];
  if (fread(ident, 1, EI_NIDENT, fp) != EI_NIDENT) {
    printf("ERROR: Failed to read file idnetifier\n");
    fclose(fp);
    return -1;
  }

  if (ident[EI_MAG0] != ELFMAG0 || ident[EI_MAG1] != ELFMAG1 ||
      ident[EI_MAG2] != ELFMAG2 || ident[EI_MAG3] != ELFMAG3) {
    fclose(fp);
    return -1;
  }

  rewind(fp);

  if (ident[EI_CLASS] == ELFCLASS32) {
    elf->bit_class = ELFCLASS32;
    fread(&elf->elf_header.h32, sizeof(Elf32_Ehdr), 1, fp);
  } else if (ident[EI_CLASS] == ELFCLASS64) {
    elf->bit_class = ELFCLASS64;
    fread(&elf->elf_header.h64, sizeof(Elf64_Ehdr), 1, fp);
  }

  return 0;
}

int read_elf_program_headers(FILE *fp, elf_generic_headers *elf) {
  if (!fp) {
    printf("ERROR: Received File Pointer is NULL\n");
    return -1;
  }

  if (elf->bit_class == ELFCLASS64) {
    Elf64_Phdr *program_headers =
        malloc(elf->elf_header.h64.e_phentsize * elf->elf_header.h64.e_phnum);
    if (program_headers == NULL) {
      printf("ERROR: failed to malloc space for program_headers\n");
      return -1;
    }
    elf->program_headers.h64 = program_headers;
    fseek(fp, elf->elf_header.h64.e_phoff, SEEK_SET);
    fread(elf->program_headers.h64, sizeof(Elf64_Phdr),
          elf->elf_header.h64.e_phnum, fp);
  } else if (elf->bit_class == ELFCLASS32) {
    Elf32_Phdr *program_headers =
        malloc(elf->elf_header.h32.e_phentsize * elf->elf_header.h32.e_phnum);
    if (program_headers == NULL) {
      printf("ERROR: failed to malloc space for program_headers\n");
      return -1;
    }
    elf->program_headers.h32 = program_headers;
    fseek(fp, elf->elf_header.h32.e_phoff, SEEK_SET);
    fread(elf->program_headers.h32, sizeof(Elf32_Phdr),
          elf->elf_header.h32.e_phnum, fp);
  } else {
    printf("ERROR: unknown elf bit class in the elf header\n");
    return -1;
  }

  return 0;
}

int read_elf_section_headers(FILE *fp, elf_generic_headers *elf) {
  if (!fp) {
    printf("ERROR: Received File Pointer is NULL\n");
    return -1;
  }

  if (elf->bit_class == ELFCLASS64) {
    Elf64_Shdr *section_headers =
        malloc(elf->elf_header.h64.e_shentsize * elf->elf_header.h64.e_shnum);
    if (section_headers == NULL) {
      printf("ERROR: failed to malloc space for section_headers\n");
      return -1;
    }
    elf->section_headers.h64 = section_headers;
    fseek(fp, elf->elf_header.h64.e_shoff, SEEK_SET);
    fread(elf->section_headers.h64, sizeof(Elf64_Shdr),
          elf->elf_header.h64.e_shnum, fp);
  } else if (elf->bit_class == ELFCLASS32) {
    Elf32_Shdr *section_headers =
        malloc(elf->elf_header.h32.e_shentsize * elf->elf_header.h32.e_shnum);
    if (section_headers == NULL) {
      printf("ERROR: failed to malloc space for section_headers\n");
      return -1;
    }
    elf->section_headers.h32 = section_headers;
    fseek(fp, elf->elf_header.h32.e_shoff, SEEK_SET);
    fread(elf->section_headers.h32, sizeof(Elf32_Shdr),
          elf->elf_header.h32.e_shnum, fp);
  } else {
    printf("ERROR: unknown elf bit class in the elf header\n");
    return -1;
  }

  return 0;
}
