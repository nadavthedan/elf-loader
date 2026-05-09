#include <elf.h>
#include <stdio.h>

#include "elfutils.h"

int main() {
  printf("Started program\n");
  int ret;
  elf_generic_headers headers;

  FILE *fp = fopen("./bins/program", "rb");
  ret = read_elf_header(fp, &headers);
  if (ret != 0) {
    printf("Failed reading elf header.\n");
    return -1;
  }

  ret = read_elf_program_header(fp, &headers);
  if (ret != 0) {
    printf("Failed reading elf program headers.\n");
    return -1;
  }

  if (headers.bit_class == ELFCLASS64) {
    printf("got in 64\n");
    printf("header size: %#x\n", headers.elf_header.h64.e_ehsize);
    printf("machine: %#x\n", headers.elf_header.h64.e_machine);
    printf("version: %#x\n", headers.elf_header.h64.e_version);
    printf("====================\n");
    for (int i = 0; i < headers.elf_header.h64.e_phnum; i++) {
      printf("Program header number %d\n", i);
      printf("Type - %#x\n", headers.program_headers.h64[i].p_type);
      printf("Offset - %ld\n", headers.program_headers.h64[i].p_offset);
    }
    printf("====================\n");
  } else if (headers.bit_class == ELFCLASS32) {
    printf("got in 32\n");
    printf("header size: %#x\n", headers.elf_header.h32.e_ehsize);
    printf("machine: %#x\n", headers.elf_header.h32.e_machine);
    printf("version: %#x\n", headers.elf_header.h32.e_version);
  }

  fclose(fp);
}
