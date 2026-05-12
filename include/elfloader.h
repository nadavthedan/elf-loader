#ifndef ELFLOADER_H
#define ELFLOADER_H

#include "elfutils.h"

int load(FILE *fp, elf_generic_headers *elf);

#endif
