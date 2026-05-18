#ifndef ELFUTILS_H
#define ELFUTILS_H

#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

// A type wiht the class of an elf header (32 bit or 64 bit)
// and a union of the header itself which shuld be used based on the bit_class.
typedef struct {
  Elf64_Ehdr elf_header;
  Elf64_Phdr *program_headers;
  Elf64_Shdr *section_headers;
} Elf64_Data;

// Reads elf header from a file to the Elf64_Headers type.
// Gets a file pointer and a pointer to Elf64_Headers which it populates.
// It returns 0 on success and -1 on error.
int elf_headers_read(FILE *fp, Elf64_Data *elf);

int populate_str_table(Elf64_Data *elf, char **str_table, uint shidx, FILE *fp);

int read_section(void **section, size_t size, Elf64_Shdr shdr, FILE *fp);
#endif
