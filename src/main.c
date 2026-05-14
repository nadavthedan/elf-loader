#include <elf.h>
#include <stdint.h>
#include <stdio.h>

#include "elfloader.h"
#include "elfutils.h"

int main() {
  printf("Started program\n");
  int ret;
  elf_generic_headers headers;

  FILE *fp = fopen("./bins/mergesort", "rb");
  ret = read_elf_header(fp, &headers);
  if (ret != 0) {
    printf("Failed reading elf header.\n");
    return -1;
  }

  ret = read_elf_program_headers(fp, &headers);
  if (ret != 0) {
    printf("Failed reading elf program headers.\n");
    return -1;
  }

  ret = read_elf_section_headers(fp, &headers);
  if (ret != 0) {
    printf("Failed reading elf section headers.\n");
    return -1;
  }

  ret = load_to_memory(fp, &headers);
  if (ret != 0) {
    printf("ERROR: failed reserveelfvm\n");
    return 1;
  }
  uintptr_t entry = headers.elf_header.h64.e_entry;

  setup_and_jump(entry, &headers);
  fclose(fp);
}
