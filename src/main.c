#include "elfloader.h"
#include "elfutils.h"
#include <elf.h>
#include <stdint.h>
#include <stdio.h>

char **main_args_parse(int argc, char *argv[]) {
  if (argc < 2) {
    return NULL;
    printf("ERROR: Program did not receive enough arguments\n");
  }
  return argv + 1;
}

int main(int argc, char *argv[]) {
  printf("Started program\n");
  int ret;
  uintptr_t base;
  int elf_argc;
  char **elf_argv;
  Elf64_Data headers;

  if ((elf_argv = main_args_parse(argc, argv)) == NULL) {
    printf("ERROR: Program received invalid arguments.\n");
    return -1;
  }
  elf_argc = argc - 1;

  FILE *fp = fopen(elf_argv[0], "rb");
  ret = elf_headers_read(fp, &headers);
  if (ret != 0) {
    printf("ERROR: Failed reading elf header.\n");
    return -1;
  }

  base = elf_static_load_to_memory(fp, &headers);
  if (base == (uintptr_t)-1) {
    printf("ERROR: failed elf static load to memory\n");
    return 1;
  }
  uintptr_t entry = headers.elf_header.e_entry;

  setup_and_jump(base + entry, base, &headers, elf_argc, elf_argv);
  fclose(fp);
}
