#include "elfloader.h"
#include "elfutils.h"
#include <elf.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#define ALIGN_DOWN(x, pagesize) ((x) & ~((pagesize) - 1))
#define ALIGN_UP(x, pagesize) (((x) + ((pagesize) - 1)) & ~((pagesize) - 1))
#define STACK_SIZE (1024 * 1024) // 1MB stack

void *elf_resolve_sym_addr(SymResolutionPtrs *symres, const char *symname,
                           uintptr_t base) {
  uint32_t hash = elf_hash((const unsigned char *)symname);
  uint32_t nbuckets = symres->sysVhashtab[0];
  uint32_t nchains = symres->sysVhashtab[1];

  uint32_t bucket_index = hash % nbuckets;
  uint32_t sym_index = symres->sysVhashtab[2 + bucket_index];

  while (sym_index != 0) {
    if (sym_index >= nchains) {
      return (void *)-1;
    }

    uint32_t name_off = symres->symboltab[sym_index].st_name;
    char *name = symres->strtab + name_off;

    if (strcmp(name, symname) == 0) {
      Elf64_Sym *sym = &symres->symboltab[sym_index];

      if (sym->st_shndx == SHN_UNDEF) {
        return (void *)-1;
      }

      if (ELF64_ST_BIND(sym->st_info) == STB_LOCAL) {
        sym_index = symres->sysVhashtab[2 + nbuckets + sym_index];
        continue;
      }

      return (void *)base + sym->st_value;
    }

    sym_index = symres->sysVhashtab[2 + nbuckets + sym_index];
  }
  return (void *)-1;
}

void *elf_resolve_sym_addr_gnu(SymResolutionPtrs *symres, const char *symname,
                               uintptr_t base) {
  if (!symres->gnu_hashtab) {
    return (void *)-1;
  }

  uint32_t *header = symres->gnu_hashtab;

  uint32_t nbuckets = header[0];
  uint32_t symndx = header[1];
  uint32_t maskwords = header[2];
  uint32_t shift2 = header[3];

  uint64_t *bloom_filter = (uint64_t *)(header + 4);
  uint32_t *buckets = (uint32_t *)(bloom_filter + maskwords);
  uint32_t *chains = buckets + nbuckets;

  uint32_t hash = elf_gnu_hash((const unsigned char *)symname);

  uint64_t bitmask_word = bloom_filter[(hash / 64) % maskwords];

  uint32_t bit1 = hash % 64;
  uint32_t bit2 = (hash >> shift2) % 64;

  if (((bitmask_word >> bit1) & 1) == 0 || ((bitmask_word >> bit2) & 1) == 0) {
    return (void *)-1;
  }

  uint32_t sym_index = buckets[hash % nbuckets];
  if (sym_index < symndx) {
    return (void *)-1;
  }

  for (;;) {
    uint32_t chain_hash = chains[sym_index - symndx];

    if ((hash & ~1) == (chain_hash & ~1)) {
      uint32_t name_off = symres->symboltab[sym_index].st_name;
      char *name = symres->strtab + name_off;

      if (strcmp(name, symname) == 0) {
        Elf64_Sym *sym = &symres->symboltab[sym_index];
        if (sym->st_shndx != SHN_UNDEF) {
          return (void *)(base + sym->st_value);
        }
      }
    }
    if (chain_hash & 1) {
      break;
    }

    sym_index++;
  }
  return (void *)-1;
}

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

int elf_ph_handle_load(Elf64_Phdr *phdr, uintptr_t base, uint64_t fd) {
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

int elf_ph_handle_dyn(Elf64_Phdr *phdr, uintptr_t base,
                      SymResolutionPtrs *symres) {
  if (phdr->p_type == PT_DYNAMIC) {
    Elf64_Dyn *dyn = (Elf64_Dyn *)(base + phdr->p_vaddr);
    Elf64_Rela *rela = NULL;
    size_t relasz = 0;
    char **needed = NULL;
    uint64_t *needed_count = 0;
    char *runpath = NULL;

    for (; dyn->d_tag != DT_NULL; dyn++) {
      if (dyn->d_tag == DT_RELA)
        rela = (Elf64_Rela *)(base + dyn->d_un.d_ptr);
      if (dyn->d_tag == DT_RELASZ)
        relasz = dyn->d_un.d_val;
      if (dyn->d_tag == DT_SYMTAB)
        symres->symboltab = (Elf64_Sym *)(base + dyn->d_un.d_ptr);
      if (dyn->d_tag == DT_STRTAB)
        symres->strtab = (char *)(base + dyn->d_un.d_ptr);
      if (dyn->d_tag == DT_HASH)
        symres->sysVhashtab = (uint32_t *)(base + dyn->d_un.d_ptr);
      if (dyn->d_tag == DT_NEEDED) {
        char **temp = realloc(needed, (*needed_count + 1) * sizeof(char *));
        if (!temp) {
          // TODO: deal with this
        }
        needed = temp;
        needed[*needed_count] = (char *)dyn->d_un.d_ptr;
        (*needed_count)++;
      }
      if (dyn->d_tag == DT_RUNPATH)
        runpath = (char *)dyn->d_un.d_ptr;
    }
    if (rela && relasz) {
      size_t count = relasz / sizeof(Elf64_Rela);
      for (size_t i = 0; i < count; i++) {
        uint64_t type = ELF64_R_TYPE(rela[i].r_info);
        uint64_t sym_idx = ELF64_M_SYM(rela[i].r_info);
        uintptr_t *target = (uintptr_t *)(base + rela[i].r_offset);
        char *name;
        switch (type) {
        case R_X86_64_RELATIVE:
          *target = base + rela[i].r_addend;
          break;
        case R_X86_64_JUMP_SLOT:
        case R_X86_64_GLOB_DAT:
          name = symres->strtab + symres->symboltab[sym_idx].st_name;
          uintptr_t addr = (uintptr_t)elf_resolve_sym_addr(symres, name, base);
          *target = addr;
          break;
        }
      }
    }
  } else {
    return -1;
  }
  return 0;
}

uintptr_t elf_load_to_memory(FILE *fp, Elf64_Data *elf) {
  uint64_t i;
  int64_t ret;
  ElfLoadVaddrBounds bounds;
  SymResolutionPtrs symres;
  void *res = elf_reserve_memory(elf, &bounds);
  uintptr_t base = (uintptr_t)res - bounds.min_vaddr;
  for (i = 0; i < elf->elf_header.e_phnum; i++) {
    Elf64_Phdr phdr = elf->program_headers[i];
    switch (phdr.p_type) {
    case PT_LOAD:
      ret = elf_ph_handle_load(&phdr, base, fileno(fp));
      break;
    case PT_DYNAMIC:
      ret = elf_ph_handle_dyn(&phdr, base, &symres);
      break;
    case PT_INTERP:
      // ret = elf_ph_interp_handle(&phdr, base);
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

uintptr_t elf_resolve_global_symbol(const char *name, LoadedLib *root_lib) {
  LoadedLib *curr = root_lib;
  while (curr != NULL) {
    uintptr_t addr = (uintptr_t)-1;
    if (curr->dyn_ptrs.symres.gnu_hashtab != NULL) {
      addr = (uintptr_t)elf_resolve_sym_addr_gnu(&curr->dyn_ptrs.symres, name,
                                                 curr->base);
    }
    if (addr == (uintptr_t)-1 && curr->dyn_ptrs.symres.sysVhashtab != NULL) {
      addr = (uintptr_t)elf_resolve_sym_addr(&curr->dyn_ptrs.symres, name,
                                             curr->base);
    }
    if (addr != (uintptr_t)-1) {
      return addr;
    }
    curr = curr->next;
  }
  return (uintptr_t)-1;
}

int16_t elf_handle_reallocations(LoadedLib *lib, LoadedLib *root_lib) {
  Elf64_Rela *rela = lib->dyn_ptrs.rela_data.rela;
  uint32_t relasz = lib->dyn_ptrs.rela_data.relasz;
  SymResolutionPtrs symres = lib->dyn_ptrs.symres;
  uintptr_t base = lib->base;

  if (rela && relasz) {
    size_t count = relasz / sizeof(Elf64_Rela);
    for (size_t i = 0; i < count; i++) {
      uint64_t type = ELF64_R_TYPE(rela[i].r_info);
      uint64_t sym_idx = ELF64_M_SYM(rela[i].r_info);
      uintptr_t *target = (uintptr_t *)(base + rela[i].r_offset);
      uintptr_t addr;
      char *name;
      switch (type) {
      case R_X86_64_RELATIVE:
        *target = base + rela[i].r_addend;
        break;
      case R_X86_64_64:
        name = symres.strtab + symres.symboltab[sym_idx].st_name;
        if (ELF64_ST_BIND(symres.symboltab[sym_idx].st_info) == STB_LOCAL) {
          addr = base + symres.symboltab[sym_idx].st_value;
        } else {
          addr = (uintptr_t)elf_resolve_global_symbol(name, root_lib);
        }
        if (addr == (uintptr_t)-1) {
          Elf64_Sym *sym = &symres.symboltab[sym_idx];
          if (ELF64_ST_BIND(sym->st_info) == STB_WEAK) {
            *target = 0;
            continue;
          }
          printf("ERROR: Linker error: failed to resolve referecne to symbol: "
                 "%s\n",
                 name);
          return -1;
        }
        *target = addr + rela[i].r_addend;
        break;
      case R_X86_64_JUMP_SLOT:
      case R_X86_64_GLOB_DAT:
        name = symres.strtab + symres.symboltab[sym_idx].st_name;
        addr = (uintptr_t)elf_resolve_global_symbol(name, root_lib);
        if (addr == (uintptr_t)-1) {
          Elf64_Sym *sym = &symres.symboltab[sym_idx];
          if (ELF64_ST_BIND(sym->st_info) == STB_WEAK) {
            *target = 0;
            continue;
          }
          printf("ERROR: Linker error: failed to resolve referecne to symbol: "
                 "%s\n",
                 name);
          return -1;
        }
        *target = addr;
        break;
      }
    }
  }

  Elf64_Rela *plt_rela = lib->dyn_ptrs.rela_data.plt_rela;
  uint32_t plt_relasz = lib->dyn_ptrs.rela_data.plt_relasz;

  if (plt_rela && plt_relasz) {
    size_t count = plt_relasz / sizeof(Elf64_Rela);
    for (size_t i = 0; i < count; i++) {
      uint64_t type = ELF64_R_TYPE(plt_rela[i].r_info);
      uint64_t sym_idx = ELF64_M_SYM(plt_rela[i].r_info);
      uintptr_t *target = (uintptr_t *)(base + plt_rela[i].r_offset);
      char *name;

      switch (type) {
      case R_X86_64_JUMP_SLOT:
        name = lib->dyn_ptrs.symres.strtab +
               lib->dyn_ptrs.symres.symboltab[sym_idx].st_name;
        uintptr_t addr = (uintptr_t)elf_resolve_global_symbol(name, root_lib);
        if (addr == (uintptr_t)-1) {
          Elf64_Sym *sym = &symres.symboltab[sym_idx];
          if (ELF64_ST_BIND(sym->st_info) == STB_WEAK) {
            *target = 0;
            continue;
          }
          printf("ERROR: Linker error: failed to resolve elf symbol: %s\n",
                 name);
          return -1;
        }
        *target = addr;
        break;
      }
    }
  }
  return 0;
}

char *find_lib_path(const char *soname) {
  const char *search_paths[] = {"/lib/x86_64-linux-gnu",
                                "/usr/lib/x86_64-linux-gnu", "/lib64", NULL};

  for (int i = 0; search_paths[i] != NULL; i++) {
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s/%s", search_paths[i], soname);

    if (access(full_path, F_OK) == 0) {
      return strdup(full_path);
    }
  }
  return NULL;
}

void elf_run_init_routines(LoadedLib *lib) {
  if (lib->dyn_ptrs.init_data.init_func) {
    void (*init_fn)(void) =
        (void (*)(void))(lib->base + lib->dyn_ptrs.init_data.init_func);
    init_fn();
  }

  if (lib->dyn_ptrs.init_data.init_array &&
      lib->dyn_ptrs.init_data.init_arraysz) {
    size_t count = lib->dyn_ptrs.init_data.init_arraysz / sizeof(uintptr_t);
    for (size_t i = 0; i < count; i++) {
      uintptr_t func_addr = lib->dyn_ptrs.init_data.init_array[i];
      if (func_addr == 0 || func_addr == (uintptr_t)-1)
        continue;
      void (*init_array_fn)(void) = (void (*)(void))func_addr;
      init_array_fn();
    }
  }
}

static LoadedLib *find_lib_by_soname(LoadedLib *root_lib, const char *soname) {
  LoadedLib *curr = root_lib;
  while (curr != NULL) {
    if (curr->soname && strcmp(curr->soname, soname) == 0) {
      return curr;
    }
    curr = curr->next;
  }
  return NULL;
}

static void elf_initialize_lib_graph(LoadedLib *lib, LoadedLib *root_lib) {
  if (lib == NULL)
    return;

  InitState state = lib->dyn_ptrs.init_data.init_state;
  if (state == INIT_STATE_COMPLETE) {
    return;
  }

  if (state == INIT_STATE_VISITING) {
    // On circular dependency the order of initialization is undefined
    // Which means it could be in any order, this loader breaks the loop cleanly
    // and executes the init only once.
    return;
  }

  lib->dyn_ptrs.init_data.init_state = INIT_STATE_VISITING;

  for (uint32_t i = 0; i < lib->dyn_ptrs.needed_data.neededsz; i++) {
    const char *dep_name = lib->dyn_ptrs.needed_data.needed[i];
    LoadedLib *dep_lib = find_lib_by_soname(root_lib, dep_name);

    if (dep_lib != NULL) {
      elf_initialize_lib_graph(dep_lib, root_lib);
    } else {
      printf("ERROR: Dependency '%s' was not found in lib graph.\n", dep_name);
    }
  }

  elf_run_init_routines(lib);

  lib->dyn_ptrs.init_data.init_state = INIT_STATE_COMPLETE;
}

int16_t elf_handle_init_execution_order(LoadedLib *root_lib) {
  if (root_lib == NULL)
    return -1;

  LoadedLib *curr = root_lib;
  while (curr != NULL) {
    curr->dyn_ptrs.init_data.init_state = INIT_STATE_UNVISITED;
    curr = curr->next;
  }
  elf_initialize_lib_graph(root_lib, root_lib);

  return 0;
}

int16_t elf_lib_already_loaded(const char *soname, LoadedLib *root_lib) {
  LoadedLib *current = root_lib;
  while (current != NULL) {
    if (soname && current->soname && strcmp(soname, current->soname) == 0) {
      return -1;
    }
    current = current->next;
  }
  return 0;
}

int16_t elf_load_lib(LoadedLib *elf_lib, LoadedLib *root_lib) {
  if (elf_lib == NULL) {
    return 0;
  }
  uint32_t i;
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
  for (i = 0; i < elf_lib->dyn_ptrs.needed_data.neededsz; i++) {
    char *needed_name = elf_lib->dyn_ptrs.needed_data.needed[i];
    if (elf_lib_already_loaded(needed_name, root_lib))
      continue;

    LoadedLib *next_lib = calloc(1, sizeof(LoadedLib));
    current->next = next_lib;
    next_lib->soname = elf_lib->dyn_ptrs.needed_data.needed[i];
    elf_load_lib(next_lib, root_lib);
    while (current->next != NULL) {
      current = current->next;
    }
  }
  int ret = fclose(fp);
  if (ret != 0) {
    // TODO: handle error.
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
