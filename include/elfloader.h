#ifndef ELFLOADER_H
#define ELFLOADER_H

#include "elfutils.h"

#define ALIGN_DOWN(x, pagesize) ((x) & ~((pagesize) - 1))
#define ALIGN_UP(x, pagesize) (((x) + ((pagesize) - 1)) & ~((pagesize) - 1))

// reserve virtual memory for the elf program LOAD data to memory.
// returns 0 on success, 1 on error.
int elf_static_load_to_memory(FILE *fp, Elf64_Data *elf);

// allocates stack, sets up initial register state jumps to entry point.
void setup_and_jump(uintptr_t entry_point, uintptr_t base, Elf64_Data *elf,
                    int argc, char *argv[]);
#endif
