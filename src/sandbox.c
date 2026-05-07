#include "elf.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
  FILE *fptr;
  elfheadident_t elfident;
  void *elfheader;
  int ret;

  printf("Sandbox start\n");
  fptr = fopen("./bins/program", "rb");

  if (fptr == NULL) {
    printf("Error: Could not open file.\n");
    return 1;
  }

  // TODO: read based on endianness.
  ret = fread(&elfident, sizeof(elfheadident_t), 1, fptr);
  printf("reaturned ret: %d\n", ret);

  char headeris64 = elfident.bitformat == 2;
  size_t headallocsize =
      headeris64 ? sizeof(elfhead_t_64) : sizeof(elfhead_t_32);
  printf("Alloc size: %zu\n", headallocsize);
  elfheader = malloc(headallocsize);
  ret = fread(elfheader, headallocsize, 1, fptr);
  printf("reaturned ret: %d\n", ret);

  if (headeris64) {
    printf("Header is 64\n");
    elfhead_t_64 *elfheader64 = (elfhead_t_64 *)elfheader;
    printf("Elf file header:\n");
    printf("magicnumber: %s\n", elfident.magicnumber);
    printf("bitformat: %d\n", elfident.bitformat);
    printf("endianness: %d\n", elfident.endianness);
    printf("headersize1: %d\n", elfheader64->headersize);
    printf("version1: %#x\n", elfheader64->version);
    printf("char: %c\n", elfident.magicnumber[3]);
  } else {
    printf("Header is 32\n");
    elfhead_t_32 *elfheader32 = (elfhead_t_32 *)elfheader;
    printf("Elf file header:\n");
    printf("magicnumber: %s\n", elfheader32->ident.magicnumber);
    printf("bitformat: %d\n", elfheader32->ident.bitformat);
    printf("endianness: %d\n", elfheader32->ident.endianness);
    printf("char: %c\n", elfheader32->ident.magicnumber[3]);
  }

  free(elfheader);
  printf("Sandbox end\n");
}
