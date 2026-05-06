#include "elf.h"
#include <stdio.h>

int main() {
  FILE *fptr;
  elfhead_t elfheader;

  printf("Sandbox start\n");
  fptr = fopen("./bins/program", "rb");

  if (fptr == NULL) {
    printf("Error: Could not open file.\n");
    return 1;
  }

  int ret = fread(&elfheader, sizeof(elfhead_t), 1, fptr);
  printf("reaturned ret: %d\n", ret);

  printf("Elf file header:\n");
  printf("magicnumber: %s\n", elfheader.magicnumber);
  printf("bitformat: %d\n", elfheader.bitformat);
  printf("endianness: %d\n", elfheader.endianness);

  printf("char: %c\n", elfheader.magicnumber[3]);

  printf("Sandbox end\n");
}
