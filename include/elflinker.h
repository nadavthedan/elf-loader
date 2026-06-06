#ifndef ELFLINKER_H
#define ELFLINKER_H

#include "elfloader.h"
#include <stdint.h>

int16_t elf_handle_reallocations(LoadedLib *lib, LoadedLib *root_lib);
int16_t elf_handle_init_execution_order(LoadedLib *lib);
uintptr_t elf_resolve_global_symbol(const char *name, LoadedLib *root_lib);
char *find_lib_path(const char *soname);
int16_t elf_lib_already_loaded(const char *soname, LoadedLib *root_lib);

#endif
