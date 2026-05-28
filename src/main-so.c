#include "elfloader.h"
#include "elfutils.h"
#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

char **main_args_parse(int argc, char *argv[]) {
  if (argc < 2) {
    printf("ERROR: Program did not receive enough arguments\n");
    return NULL;
  }
  return argv + 1;
}

int main(int argc, char *argv[]) {
  printf("Started program\n");
  int ret;
  int elf_argc;
  char **elf_argv;
  LoadedLib *lib = calloc(1, sizeof(LoadedLib));

  if ((elf_argv = main_args_parse(argc, argv)) == NULL) {
    printf("ERROR: Program received invalid arguments.\n");
    return -1;
  }
  elf_argc = argc - 1;

  char *program_name = elf_argv[0];
  lib->soname = program_name;
  ret = elf_load_lib(lib);
  if (ret != 0) {
    printf("ERROR: Failed Loading .so Library.\n");
    return -1;
  }

  ret = elf_handle_reallocations(lib);
  if (ret != 0) {
    printf("ERROR: Failed Relocations for .so Library.\n");
    return -1;
  }
  ret = elf_handle_init_execution_order(lib);
  if (ret != 0) {
    printf("ERROR: Failed Init for .so Library.\n");
    return -1;
  }

  setup_and_jump(lib->base + lib->headers.elf_header.e_entry, lib->base,
                 &lib->headers, elf_argc, elf_argv);
}
