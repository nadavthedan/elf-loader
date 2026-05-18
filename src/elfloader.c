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

typedef struct {
  uintptr_t min_vaddr;
  uintptr_t max_vaddr;
} ElfLoadVaddrBounds;

int elf_calculate_total_vaddr(Elf64_Data *elf, ElfLoadVaddrBounds *bounds) {
  uint16_t i;
  uintptr_t min_vaddr = -1;
  uintptr_t max_vaddr = 0;
  Elf64_Phdr phdr;
  if (elf->elf_header.e_phnum <= 0)
    return 1;
  for (i = 0; i < elf->elf_header.e_phnum; i++) {
    phdr = elf->program_headers[i];
    if (phdr.p_type != PT_LOAD)
      continue;

    min_vaddr = phdr.p_vaddr < min_vaddr ? phdr.p_vaddr : min_vaddr;
    max_vaddr = (phdr.p_vaddr + phdr.p_memsz) > max_vaddr
                    ? (phdr.p_vaddr + phdr.p_memsz)
                    : max_vaddr;
  }
  bounds->max_vaddr = max_vaddr;
  bounds->min_vaddr = min_vaddr;

  return 0;
}

void *elf_reserve_memory(Elf64_Data *elf, ElfLoadVaddrBounds *bounds) {
  int ret;
  int page_size = getpagesize();
  ret = elf_calculate_total_vaddr(elf, bounds);
  if (ret != 0) {
    printf("ERROR: Failed calculating bounds");
  }
  uint total_size = ALIGN_UP(bounds->max_vaddr - bounds->min_vaddr, page_size);
  void *mapping;
  switch (elf->elf_header.e_type) {
  case ET_EXEC:
    mapping = mmap((void *)bounds->min_vaddr, total_size, PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    break;
  default:
    mapping =
        mmap(NULL, total_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  }

  return mapping;
}

int elf_ph_load_handle(Elf64_Phdr *phdr, int fd, uintptr_t base) {
  int page_size = getpagesize();
  uint32_t prots = PROT_NONE;

  uintptr_t seg_start = ALIGN_DOWN(phdr->p_vaddr, page_size);
  uintptr_t page_offset = phdr->p_vaddr - seg_start;
  size_t map_size = ALIGN_UP(page_offset + phdr->p_memsz, page_size);
  off_t file_off = ALIGN_DOWN(phdr->p_offset, page_size);

  if (phdr->p_flags & PF_X)
    prots |= PROT_EXEC;
  if (phdr->p_flags & PF_W)
    prots |= PROT_WRITE;
  if (phdr->p_flags & PF_R)
    prots |= PROT_READ;

  void *mem = mmap((void *)(base + seg_start), map_size, prots,
                   MAP_FIXED | MAP_PRIVATE, fd, file_off);
  if (mem == MAP_FAILED) {
    printf("ERROR: Failed mmap.\n");
    return -1;
  }

  uintptr_t data_offset = phdr->p_vaddr - seg_start;
  if (phdr->p_filesz < phdr->p_memsz) {
    memset(mem + phdr->p_filesz + data_offset, 0,
           phdr->p_memsz - phdr->p_filesz);
  }
  return 0;
}

int elf_ph_dyn_handle(Elf64_Phdr *phdr, uintptr_t base) {
  if (phdr->p_type == PT_DYNAMIC) {
    Elf64_Dyn *dyn = (Elf64_Dyn *)(base + phdr->p_vaddr);
    Elf64_Rela *rela = NULL;
    size_t relasz = 0;

    for (; dyn->d_tag != DT_NULL; dyn++) {
      if (dyn->d_tag == DT_RELA)
        rela = (Elf64_Rela *)(base + dyn->d_un.d_ptr);
      if (dyn->d_tag == DT_RELASZ)
        relasz = dyn->d_un.d_val;
    }
    if (rela && relasz) {
      size_t count = relasz / sizeof(Elf64_Rela);
      for (size_t j = 0; j < count; j++) {
        if (ELF64_R_TYPE(rela[j].r_info) == R_X86_64_RELATIVE) {
          uintptr_t *patch_addr = (uintptr_t *)(base + rela[j].r_offset);
          *patch_addr = base + rela[j].r_addend;
        }
      }
    }
  }
  return 0;
}

uintptr_t elf_load_to_memory(FILE *fp, Elf64_Data *elf) {
  uint64_t i;
  int64_t ret;
  ElfLoadVaddrBounds bounds;
  void *res = elf_reserve_memory(elf, &bounds);
  uintptr_t base = (uintptr_t)res - bounds.min_vaddr;
  for (i = 0; i < elf->elf_header.e_phnum; i++) {
    Elf64_Phdr phdr = elf->program_headers[i];
    switch (phdr.p_type) {
    case PT_LOAD:
      ret = elf_ph_load_handle(&phdr, fileno(fp), base);
      break;
    case PT_DYNAMIC:
      ret = elf_ph_dyn_handle(&phdr, base);
      break;
    }
    if (ret == -1) {
      printf("ERROR: Failed to load elf to memory. faild on program header of "
             "type %d",
             phdr.p_type);
      return -1;
    }
  }
  return base;
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
  *(--stack_ptr) = 0;

  // argv (NULL terminated)
  *(--stack_ptr) = 0;
  for (int i = argc - 1; i >= 0; i--) {
    *(--stack_ptr) = (uintptr_t)argv[i];
  }

  // argc
  *(--stack_ptr) = argc;

  execute_entry(entry_point, stack_ptr);
}
