#include "elfexec.h"
#include <elf.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define STACK_SIZE (1024 * 1024) // 1MB stack

static __attribute__((naked)) void execute_entry(uintptr_t entry,
                                                 void *stack_ptr) {
  // Accourding to the AMD64 ABI calling convention:
  // first argument entry, arrives in the %rdi register
  // second argument stack_ptr, arrives in the %rsi register
  __asm__ volatile(
      "mov %%rsi, %%rsp\n\t" // Cut over to the custom stack pointer
      "xor %%rdx, %%rdx\n\t" // Clear %rdx per ABI
      "jmp *%%rdi\n\t"       // Jump to the ELF entry point
      :
      :
      : "memory");
}

void setup_and_jump(uintptr_t entry_point, uintptr_t base, Elf64_Data *elf,
                    int argc, char *argv[]) {
  int page_size = getpagesize();

  void *stack_low = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
  if (stack_low == MAP_FAILED) {
    printf("ERROR: Failed to allocate stack.\n");
    _exit(1);
  }

  uint64_t *stack_ptr = (uint64_t *)((uintptr_t)stack_low + STACK_SIZE);

  // Calculate program headers virtual address in the loaded image
  uintptr_t phdr_addr = 0;
  uint16_t phnum = 0;
  phnum = elf->elf_header.e_phnum;
  for (int i = 0; i < phnum; i++) {
    if (elf->program_headers[i].p_type == PT_LOAD) {
      uintptr_t seg_start =
          ALIGN_DOWN(elf->program_headers[i].p_vaddr, page_size);
      off_t file_off = ALIGN_DOWN(elf->program_headers[i].p_offset, page_size);
      phdr_addr =
          base + seg_start + (elf->elf_header.e_phoff - (uint64_t)file_off);
      break;
    }
  }

  int total_pushes = 2; // for random_data[0] and random_data[1]
  int aux_count = 6; // AT_NULL, AT_RANDOM, AT_PAGESZ, AT_PHNUM, AT_ENTRY pairs
  if (phdr_addr != 0)
    aux_count++;

  total_pushes +=
      (aux_count * 2);  // Each aux entry is a type + value pair (2 slots)
  total_pushes += 1;    // envp NULL terminator
  total_pushes += 1;    // argv NULL terminator
  total_pushes += argc; // the actual argv pointers
  total_pushes += 1;    // argc itself
  if (total_pushes % 2 != 0) {
    --stack_ptr; // Drop an extra 8 bytes to act as padding
  }

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
  *(--stack_ptr) = 0; // TODO: add env support

  // argv (NULL terminated)
  *(--stack_ptr) = 0;
  for (int i = argc - 1; i >= 0; i--) {
    *(--stack_ptr) = (uintptr_t)argv[i];
  }

  // argc
  *(--stack_ptr) = argc;

  execute_entry(entry_point, stack_ptr);
}
