#include "elfloader.h"
#include "elfutils.h"
#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

  if ((elf_argv = main_args_parse(argc, argv)) == NULL) {
    printf("ERROR: Program received invalid arguments.\n");
    return -1;
  }
  elf_argc = argc - 1;

  LoadedLib *lib = calloc(1, sizeof(LoadedLib));
  if (!lib) {
    printf("ERROR: Failed to allocate lib.\n");
    return -1;
  }

  char *program_name = elf_argv[0];
  char *resolved_path = program_name;

  if (strchr(program_name, '/') == NULL) {
    size_t len = strlen(program_name);
    char *local_path = malloc(len + 3);
    if (local_path) {
      snprintf(local_path, len + 3, "./%s", program_name);
      resolved_path = local_path;
    }
  } else {
    resolved_path = strdup(program_name);
  }

  lib->soname = resolved_path;

  ret = elf_load_lib(lib, lib);
  if (ret != 0) {
    printf("ERROR: Failed Loading .so Library.\n");
    free(resolved_path);
    free(lib);
    return -1;
  }

  LoadedLib *curr = lib;
  while (curr != NULL) {
    ret = elf_handle_reallocations(curr, lib);
    if (ret != 0) {
      printf("ERROR: Failed Relocations for .so Library.\n");
      return -1;
    }
    curr = curr->next;
  }
  ret = elf_handle_init_execution_order(lib);
  if (ret != 0) {
    printf("ERROR: Failed Init for .so Library.\n");
    return -1;
  }

  uintptr_t main_addr = elf_resolve_global_symbol("main", lib);
  if (main_addr == (uintptr_t)-1) {
    printf("ERROR: Could not find 'main' symbol in loaded library.\n");
    return -1;
  }

  typedef int (*main_fn_t)(int, char **);
  main_fn_t main_fn = (main_fn_t)main_addr;
  ret = main_fn(elf_argc, elf_argv);

  return ret;
}
