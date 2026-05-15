#include <elf.h>
#include <stdint.h>
#include <stdio.h>

#include "elfloader.h"
#include "elfutils.h"

int main() {
  printf("Started program\n");
  int ret;
  Elf64_Headers headers;

  FILE *fp = fopen("./bins/mergesort", "rb");
  ret = elf_headers_read(fp, &headers);
  if (ret != 0) {
    printf("Failed reading elf header.\n");
    return -1;
  }

  ret = load_to_memory(fp, &headers);
  if (ret != 0) {
    printf("ERROR: failed reserveelfvm\n");
    return 1;
  }
  uintptr_t entry = headers.elf_header.e_entry;

  setup_and_jump(entry, &headers);
  fclose(fp);
}
