#include "elfloader.h"
#include "elflinker.h"
#include <dlfcn.h>
#include <elf.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

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

void elf_handle_load(Elf64_Data *elf, uintptr_t base, int fd) {
  int page_size = getpagesize();
  uint64_t i;
  for (i = 0; i < elf->elf_header.e_phnum; i++) {
    Elf64_Phdr phdr = elf->program_headers[i];
    if (phdr.p_type != PT_LOAD)
      continue;

    uint32_t prots = PROT_NONE;
    if (phdr.p_flags & PF_X)
      prots |= PROT_EXEC;
    if (phdr.p_flags & PF_W)
      prots |= PROT_WRITE;
    if (phdr.p_flags & PF_R)
      prots |= PROT_READ;

    uintptr_t seg_start = ALIGN_DOWN(phdr.p_vaddr, page_size);
    uintptr_t file_end_vaddr = phdr.p_vaddr + phdr.p_filesz;
    uintptr_t file_end_page = ALIGN_UP(file_end_vaddr, page_size);
    uintptr_t mem_end_vaddr = phdr.p_vaddr + phdr.p_memsz;
    uintptr_t mem_end_page = ALIGN_UP(mem_end_vaddr, page_size);

    size_t file_map_size = file_end_page - seg_start;
    off_t file_off = ALIGN_DOWN(phdr.p_offset, page_size);

    void *mem =
        mmap((void *)(base + seg_start), file_map_size, PROT_READ | PROT_WRITE,
             MAP_FIXED | MAP_PRIVATE, fd, file_off);

    if (mem == MAP_FAILED) {
      printf("ERROR: Failed mmap.\n");
      return; // TODO: handle
    }

    // Handle BSS allocation
    if (phdr.p_filesz < phdr.p_memsz) {
      size_t partial_zero_len = file_end_page - file_end_vaddr;
      // Edgecase: bss allocation fits inside the same page,
      // clamp zeroing length to actual end of memory.
      if (mem_end_vaddr < file_end_page) {
        partial_zero_len = mem_end_vaddr - file_end_vaddr;
      }

      if (partial_zero_len > 0) {
        memset((void *)(base + file_end_vaddr), 0, partial_zero_len);
      }

      // BSS spills over into the subsequent pages,
      // map as pure anonymous memory.
      if (mem_end_page > file_end_page) {
        size_t anon_map_size = mem_end_page - file_end_page;

        void *anon_mem = mmap((void *)(base + file_end_page), anon_map_size,
                              PROT_READ | PROT_WRITE,
                              MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (anon_mem == MAP_FAILED) {
          printf("ERROR: Failed anonymous mmap for extra BSS pages.\n");
          return; // TODO: handle
        }
      }
    }

    size_t total_map_size = mem_end_page - seg_start;
    if (mprotect((void *)(base + seg_start), total_map_size, prots) != 0) {
      printf("ERROR: Failed to mprotect PT_LOAD segment.\n");
      return; // TODO: Handle
    }
  }

  return; // TODO: handle success
}

void elf_handle_dyn(Elf64_Data *elf, uintptr_t base, DynPtrs *dyn_ptrs) {
  uint64_t i;
  for (i = 0; i < elf->elf_header.e_phnum; i++) {
    Elf64_Phdr phdr = elf->program_headers[i];
    if (phdr.p_type == PT_DYNAMIC) {
      Elf64_Dyn *dyn = (Elf64_Dyn *)(base + phdr.p_vaddr);

      Elf64_Dyn *cursor = dyn;
      for (; cursor->d_tag != DT_NULL; cursor++) {
        char **temp;
        switch (cursor->d_tag) {
        case DT_RELA:
          dyn_ptrs->rela_data.rela = (Elf64_Rela *)(base + cursor->d_un.d_ptr);
          break;
        case DT_RELASZ:
          dyn_ptrs->rela_data.relasz = cursor->d_un.d_val;
          break;
        case DT_JMPREL:
          dyn_ptrs->rela_data.plt_rela =
              (Elf64_Rela *)(base + cursor->d_un.d_ptr);
          break;
        case DT_PLTRELSZ:
          dyn_ptrs->rela_data.plt_relasz = cursor->d_un.d_val;
          break;
        case DT_RELR:
          dyn_ptrs->rela_data.relr = (Elf64_Relr *)(base + cursor->d_un.d_ptr);
          break;
        case DT_RELRSZ:
          dyn_ptrs->rela_data.relrsz = cursor->d_un.d_val;
          break;
        case DT_SYMTAB:
          dyn_ptrs->symres.symboltab = (Elf64_Sym *)(base + cursor->d_un.d_ptr);
          break;
        case DT_STRTAB:
          dyn_ptrs->symres.strtab = (char *)(base + cursor->d_un.d_ptr);
          break;
        case DT_HASH:
          dyn_ptrs->symres.sysVhashtab =
              (uint32_t *)(base + cursor->d_un.d_ptr);
          break;
        case DT_RUNPATH:
          dyn_ptrs->needed_data.runpath = (char *)cursor->d_un.d_ptr;
          break;
        case DT_INIT:
          dyn_ptrs->init_data.init_func = cursor->d_un.d_ptr;
          break;
        case DT_INIT_ARRAY:
          dyn_ptrs->init_data.init_array =
              (uintptr_t *)(base + cursor->d_un.d_ptr);
          break;
        case DT_INIT_ARRAYSZ:
          dyn_ptrs->init_data.init_arraysz = cursor->d_un.d_val;
          break;
        case DT_GNU_HASH:
          dyn_ptrs->symres.gnu_hashtab =
              (uint32_t *)(base + cursor->d_un.d_ptr);
          break;
        case DT_NEEDED:
          temp = realloc(dyn_ptrs->needed_data.needed,
                         (dyn_ptrs->needed_data.neededsz + 1) * sizeof(char *));
          if (!temp) {
            // TODO: deal with this
          }
          dyn_ptrs->needed_data.needed = temp;
          dyn_ptrs->needed_data.needed[dyn_ptrs->needed_data.neededsz] =
              (char *)cursor->d_un.d_val;
          (dyn_ptrs->needed_data.neededsz)++;
          break;
        }
      }
    }
  }
  for (uint32_t j = 0; j < dyn_ptrs->needed_data.neededsz; j++) {
    uintptr_t offset = (uintptr_t)dyn_ptrs->needed_data.needed[j];
    dyn_ptrs->needed_data.needed[j] = dyn_ptrs->symres.strtab + offset;
  }
}

int16_t elf_load_lib(LoadedLib *elf_lib, LoadedLib *root_lib) {
  if (elf_lib == NULL) {
    return 0;
  }
  if (strchr(elf_lib->soname, '/') != NULL) {
    elf_lib->path = strdup(elf_lib->soname);
  } else {
    char *lib_path = find_lib_path(elf_lib->soname);
    if (lib_path != NULL) {
      elf_lib->path = lib_path;
    } else {
      printf("ERROR: couldn't resolve path for %s\n", elf_lib->soname);
      return (uint16_t)-1;
    }
  }
  FILE *fp = fopen(elf_lib->path, "rb");
  elf_headers_read(fp, &elf_lib->headers);
  ElfLoadVaddrBounds lib_bounds;
  void *res = elf_reserve_memory(&elf_lib->headers, &lib_bounds);
  elf_lib->base = (uintptr_t)res - lib_bounds.min_vaddr;
  elf_handle_load(&elf_lib->headers, elf_lib->base, fileno(fp));
  elf_handle_dyn(&elf_lib->headers, elf_lib->base, &elf_lib->dyn_ptrs);

  LoadedLib *current = elf_lib;
  for (uint32_t i = 0; i < elf_lib->dyn_ptrs.needed_data.neededsz; i++) {
    char *needed_name = elf_lib->dyn_ptrs.needed_data.needed[i];

    void *host_handle = dlopen(needed_name, RTLD_NOLOAD | RTLD_LAZY);
    if (host_handle) {
      dlclose(host_handle);
      continue;
    }

    if (elf_lib_already_loaded(needed_name, root_lib))
      continue;

    LoadedLib *next_lib = calloc(1, sizeof(LoadedLib));
    current->next = next_lib;
    next_lib->soname = elf_lib->dyn_ptrs.needed_data.needed[i];
    elf_load_lib(next_lib, root_lib);
    while (current->next != NULL)
      current = current->next;
  }

  fclose(fp);

  return 0;
}
