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
#define STACK_SIZE (1024 * 1024) // 1MB stack

int load_to_memory(FILE *fp, Elf64_Headers *elf) {
  int i;
  int page_size = getpagesize();
  for (i = 0; i < elf->elf_header.e_phnum; i++) {
    Elf64_Phdr phdr = elf->program_headers[i];
    uint32_t prots = PROT_NONE;
    if (phdr.p_type == PT_LOAD) {
      uintptr_t seg_start = ALIGN_DOWN(phdr.p_vaddr, page_size);
      uintptr_t seg_end = ALIGN_UP(phdr.p_vaddr + phdr.p_memsz, page_size);

      size_t seg_size = seg_end - seg_start;

      off_t file_off = ALIGN_DOWN(phdr.p_offset, page_size);

      if (phdr.p_flags & PF_X) {
        prots |= PROT_EXEC;
      }
      if (phdr.p_flags & PF_W)
        prots |= PROT_WRITE;
      if (phdr.p_flags & PF_R)
        prots |= PROT_READ;
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
    }
  }
  return 0;
}

void execute_entry(uintptr_t entry, void *stack_ptr) {
  __asm__ volatile(
      "mov %0, %%rsp\n\t"    // Set the stack pointer to our new stack
      "xor %%rdx, %%rdx\n\t" // Per ABI: rdx should be 0 to indicate no atexit
      "jmp *%1\n\t"          // Jump to the ELF entry point
      :
      : "r"(stack_ptr), "c"(entry)
      : "memory");
}

void setup_and_jump(uintptr_t entry_point, Elf64_Headers *elf) {
  int page_size = getpagesize();

  void *stack_low = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
  if (stack_low == MAP_FAILED) {
    printf("ERROR: Failed to allocate stack.\n");
    _exit(1);
  }

  // 2. Point to the "top" of the stack (it grows down).
  // stack is from stack_low to stack_low + STACK_SIZE - 1, so the top is
  // stack_low + STACK_SIZE.
  uint64_t *stack_ptr = (uint64_t *)((uintptr_t)stack_low + STACK_SIZE);

  // 16 random bytes for AT_RANDOM (stack canary seed)
  uint64_t random_data[2] = {0};
  FILE *urand = fopen("/dev/urandom", "rb");
  if (urand) {
    fread(random_data, 1, 16, urand);
    fclose(urand);
  }

  *(--stack_ptr) = random_data[1];
  *(--stack_ptr) = random_data[0];
  uintptr_t rand_addr = (uintptr_t)stack_ptr;

  // Calculate program headers virtual address in the loaded image
  uintptr_t phdr_addr = 0;
  uint16_t phnum = 0;
  phnum = elf->elf_header.e_phnum;
  for (int i = 0; i < phnum; i++) {
    if (elf->program_headers[i].p_type == PT_LOAD) {
      uintptr_t seg_start =
          ALIGN_DOWN(elf->program_headers[i].p_vaddr, page_size);
      off_t file_off = ALIGN_DOWN(elf->program_headers[i].p_offset, page_size);
      phdr_addr = seg_start + (elf->elf_header.e_phoff - (uint64_t)file_off);
      break;
    }
  }

  // Push auxiliary vector (high to low address)
  *(--stack_ptr) = 0; // AT_NULL a_val
  *(--stack_ptr) = 0; // AT_NULL a_type

  *(--stack_ptr) = rand_addr;
  *(--stack_ptr) = AT_RANDOM;

  *(--stack_ptr) = (uint64_t)page_size;
  *(--stack_ptr) = AT_PAGESZ;

  if (phdr_addr != 0) {
    *(--stack_ptr) = (uint64_t)phdr_addr;
    *(--stack_ptr) = AT_PHDR;
  }

  *(--stack_ptr) = (uint64_t)phnum;
  *(--stack_ptr) = AT_PHNUM;

  *(--stack_ptr) = (uint64_t)entry_point;
  *(--stack_ptr) = AT_ENTRY;

  // envp (NULL terminated)
  *(--stack_ptr) = 0;

  // argv (NULL terminated)
  *(--stack_ptr) = 0;
  char *arg0 = "./mergesort";
  *(--stack_ptr) = (uintptr_t)arg0;

  // argc
  *(--stack_ptr) = 1;

  execute_entry(entry_point, stack_ptr);
}
