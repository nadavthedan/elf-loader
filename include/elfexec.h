#ifndef ELFEXEC_H
#define ELFEXEC_H

#include "elfloader.h"
#include <stdint.h>

void setup_and_jump(uintptr_t entry_point, uintptr_t base, Elf64_Data *elf,
                    int argc, char *argv[]);

#endif
