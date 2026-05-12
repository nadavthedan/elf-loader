#include "elfutils.h"
#include <elf.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define ALIGN_DOWN(x, pagesize) ((x) & ~((pagesize) - 1))
#define ALIGN_UP(x, pagesize) (((x) + ((pagesize) - 1)) & ~((pagesize) - 1))

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
          prots |= PROT_EXEC;
        }
        if (phdr.p_flags & PF_W) {
          prots |= PROT_WRITE;
        }
        if (phdr.p_flags & PF_R) {
          prots |= PROT_READ;
        }
        printf("virtural mem: seg_start: %zu, seg_size: %zu, file_off: %jd\n",
               seg_start, seg_size, file_off);
        void *mem = mmap((void *)seg_start, seg_size, prots,
                         MAP_FIXED | MAP_PRIVATE, fileno(fp), file_off);
        if (mem == MAP_FAILED) {
          printf("ERROR: Failed mmap.\n");
          return 1;
        }

        uintptr_t data_offset = phdr.p_vaddr - seg_start;
        if (phdr.p_filesz < phdr.p_memsz) {
          memset(mem + phdr.p_filesz + data_offset, 0,
                 phdr.p_memsz - phdr.p_filesz);
        }

        printf("mem: %p\n", mem);
      }
    }
  } else if (elf->bit_class == ELFCLASS32) {
    for (i = 0; i < elf->elf_header.h32.e_phnum; i++) {
      Elf32_Phdr phdr = elf->program_headers.h32[i];
      uint32_t prots = PROT_NONE;
      if (phdr.p_type == PT_LOAD) {
        uintptr_t seg_start = ALIGN_DOWN(phdr.p_vaddr, page_size);
        uintptr_t seg_end = ALIGN_UP(phdr.p_vaddr + phdr.p_memsz, page_size);

        size_t seg_size = seg_end - seg_start;

        off_t file_off = ALIGN_DOWN(phdr.p_offset, page_size);

        if (phdr.p_flags & PF_X) {
          prots |= PROT_EXEC;
        }
        if (phdr.p_flags & PF_W) {
          prots |= PROT_WRITE;
        }
        if (phdr.p_flags & PF_R) {
          prots |= PROT_READ;
        }
        printf("virtural mem: seg_start: %zu, seg_size: %zu, file_off: %jd\n",
               seg_start, seg_size, file_off);
        void *mem = mmap((void *)seg_start, seg_size, prots,
                         MAP_FIXED | MAP_PRIVATE, fileno(fp), file_off);
        if (mem == MAP_FAILED) {
          printf("ERROR: Failed mmap.\n");
          return 1;
        }

        uintptr_t data_offset = phdr.p_vaddr - seg_start;
        if (phdr.p_filesz < phdr.p_memsz) {
          memset(mem + phdr.p_filesz + data_offset, 0,
                 phdr.p_memsz - phdr.p_filesz);
        }

        printf("mem: %p\n", mem);
      }
    }
  } else {
    printf("ERROR: unknown bit_class: %d", elf->bit_class);
    return 1;
  }
  return 0;
}
