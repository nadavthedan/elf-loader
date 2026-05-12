#ifndef ELFLOADER_H
#define ELFLOADER_H

#include "elfutils.h"

#define ALIGN_DOWN(x, pagesize) ((x) & ~((pagesize) - 1))
#define ALIGN_UP(x, pagesize) (((x) + ((pagesize) - 1)) & ~((pagesize) - 1))

// reserve virtual memory for the elf program LOAD data to memory.
// returns 0 on success, 1 on error.
int reserveelfvm(FILE *fp, elf_generic_headers *elf);

#endif
