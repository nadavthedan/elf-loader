#include "elfutils.h"
#include <elf.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#define ALIGN_DOWN(x, pagesize) ((x) & ~((pagesize) - 1))
#define ALIGN_UP(x, pagesize) ((x) + ((pagesize) - 1) & ~((pagesize) - 1))

int load(FILE *fp, elf_generic_headers *elf) {
  int i;
  int page_size = getpagesize();
  if (elf->bit_class == ELFCLASS64) {
    for (i = 0; i < elf->elf_header.h64.e_phnum; i++) {
      Elf64_Phdr phdr = elf->program_headers.h64[i];
      uint32_t prots = PROT_NONE;
      if (phdr.p_type == PT_LOAD) {
        uintptr_t seg_start = ALIGN_DOWN(phdr.p_vaddr, page_size);
        uintptr_t seg_end = ALIGN_UP(phdr.p_vaddr + phdr.p_memsz, page_size);

        size_t seg_size = seg_end - seg_start;

        off_t file_off = ALIGN_DOWN(phdr.p_offset, page_size);

        if (phdr.p_flags & PF_X) {
          prots += PROT_EXEC;
        }
        if (phdr.p_flags & PF_W) {
          prots += PROT_WRITE;
        }
        if (phdr.p_flags & PF_R) {
          prots += PROT_READ;
        }

        int *addr = mmap((void *)seg_start, seg_size, PROT_READ, prots,
                         fileno(fp), file_off);
      }
    }
  } else if (elf->bit_class == ELFCLASS32) {
  } else {
    return 1;
  }
  return 0;
}
