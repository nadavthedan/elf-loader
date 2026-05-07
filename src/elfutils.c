#include <elf.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
  uint8_t bit_class;
  union {
    Elf32_Ehdr h32;
    Elf64_Ehdr h64;
  } header;
} elf_generic_header;

int read_elf_header(char *filename, elf_generic_header *ret_header) {
  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    printf("Failed to open file %s\n", filename);
    return -1;
  }

  unsigned char ident[EI_NIDENT];
  if (fread(ident, 1, EI_NIDENT, fp) != EI_NIDENT) {
    printf("Failed to read file idnetifier\n");
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
    ret_header->bit_class = ELFCLASS32;
    fread(&ret_header->header.h32, sizeof(Elf32_Ehdr), 1, fp);
  } else if (ident[EI_CLASS] == ELFCLASS64) {
    ret_header->bit_class = ELFCLASS64;
    fread(&ret_header->header.h64, sizeof(Elf64_Ehdr), 1, fp);
  }

  fclose(fp);
  return 0;
}
