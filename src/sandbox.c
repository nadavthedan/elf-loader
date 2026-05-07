#include <elf.h>
#include <stdio.h>

#include "elfutils.h"

int main() {
  int ret;
  elf_generic_header header;

  ret = read_elf_header("./bins/program", &header);
  if (ret != 0) {
    printf("Failed reading elf header.");
    return -1;
  }

  if (header.bit_class == ELFCLASS64) {
    printf("got in 64\n");
    printf("header size: %#x\n", header.header.h64.e_ehsize);
    printf("machine: %#x\n", header.header.h64.e_machine);
    printf("version: %#x\n", header.header.h64.e_version);
  } else if (header.bit_class == ELFCLASS32) {
    printf("got in 32\n");
    printf("header size: %#x\n", header.header.h32.e_ehsize);
    printf("machine: %#x\n", header.header.h32.e_machine);
    printf("version: %#x\n", header.header.h32.e_version);
  }
}
