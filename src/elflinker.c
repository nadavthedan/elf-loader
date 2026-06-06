#include "elflinker.h"
#include <dlfcn.h>
#include <elf.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void *elf_resolve_sym_addr(SymResolutionPtrs *symres,
                                  const char *symname, uintptr_t base) {
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

static void *elf_resolve_sym_addr_gnu(SymResolutionPtrs *symres,
                                      const char *symname, uintptr_t base) {
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

  void *sym = dlsym(RTLD_DEFAULT, name);
  if (sym) {
    return (uintptr_t)sym;
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
      uint64_t sym_idx = ELF64_R_SYM(rela[i].r_info);
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
      uint64_t sym_idx = ELF64_R_SYM(plt_rela[i].r_info);
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
  Elf64_Relr *relr = lib->dyn_ptrs.rela_data.relr;
  uint32_t relrsz = lib->dyn_ptrs.rela_data.relrsz;

  if (relr && relrsz) {
    size_t count = relrsz / sizeof(Elf64_Relr);
    uintptr_t group_offset = 0;
    for (size_t i = 0; i < count; i++) {
      Elf64_Relr entry = relr[i];
      if ((entry & 1) == 0) {
        uintptr_t offset = entry;
        *(uintptr_t *)(base + offset) += base;
        group_offset = offset + sizeof(uintptr_t);
      } else {
        uintptr_t offset = group_offset;
        entry >>= 1;
        do {
          if (entry & 1) {
            *(uintptr_t *)(base + offset) += base;
          }
          offset += sizeof(uintptr_t);
          entry >>= 1;
        } while (entry != 0);
        group_offset = offset;
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

static void elf_run_init_routines(LoadedLib *lib) {
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
    return;
  }

  lib->dyn_ptrs.init_data.init_state = INIT_STATE_VISITING;

  for (uint32_t i = 0; i < lib->dyn_ptrs.needed_data.neededsz; i++) {
    const char *dep_name = lib->dyn_ptrs.needed_data.needed[i];
    LoadedLib *dep_lib = find_lib_by_soname(root_lib, dep_name);

    if (dep_lib != NULL) {
      elf_initialize_lib_graph(dep_lib, root_lib);
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
