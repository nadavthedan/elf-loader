#ifndef ELFLOADER_H
#define ELFLOADER_H

#include "elfutils.h"
#include <elf.h>
#include <stddef.h>
#include <stdint.h>

#define ALIGN_DOWN(x, pagesize) ((x) & ~((pagesize) - 1))
#define ALIGN_UP(x, pagesize) (((x) + ((pagesize) - 1)) & ~((pagesize) - 1))

typedef struct {
  uintptr_t min_vaddr;
  uintptr_t max_vaddr;
} ElfLoadVaddrBounds;

typedef struct {
  uint32_t *sysVhashtab;
  uint32_t *gnu_hashtab;
  Elf64_Sym *symboltab;
  char *strtab;
} SymResolutionPtrs;

typedef uint64_t Elf64_Relr;

typedef struct {
  Elf64_Rela *rela;
  uint32_t relasz;
  Elf64_Rela *plt_rela;
  uint32_t plt_relasz;
  Elf64_Relr *relr;
  uint32_t relrsz;
} RelaData;

typedef struct {
  char **needed;
  uint32_t neededsz;
  char *runpath;
} NeededData;

typedef enum {
  INIT_STATE_UNVISITED = 0,
  INIT_STATE_VISITING,
  INIT_STATE_COMPLETE
} InitState;

typedef struct {
  uintptr_t init_func;
  uintptr_t *init_array;
  size_t init_arraysz;
  InitState init_state;
} InitData;

typedef struct {
  SymResolutionPtrs symres;
  RelaData rela_data;
  NeededData needed_data;
  InitData init_data;
} DynPtrs;

typedef struct LoadedLib {
  char *soname;
  char *path;
  uintptr_t base;
  Elf64_Data headers;
  DynPtrs dyn_ptrs;
  struct LoadedLib *next;
} LoadedLib;

int16_t elf_handle_reallocations(LoadedLib *lib, LoadedLib *root_lib);
int16_t elf_handle_init_execution_order(LoadedLib *lib);
int16_t elf_load_lib(LoadedLib *elf_lib, LoadedLib *root_lib);
uintptr_t elf_resolve_global_symbol(const char *name, LoadedLib *root_lib);

// allocates stack, sets up initial register state jumps to entry point.
void setup_and_jump(uintptr_t entry_point, uintptr_t base, Elf64_Data *elf,
                    int argc, char *argv[]);
#endif
