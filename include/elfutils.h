#ifndef ELFUTILS_H
#define ELFUTILS_H

#include <elf.h>
#include <stdint.h>
#include <stdio.h>

// A type wiht the class of an elf header (32 bit or 64 bit)
// and a union of the header itself which shuld be used based on the bit_class.
typedef struct {
  Elf64_Ehdr elf_header;
  Elf64_Phdr *program_headers;
  Elf64_Shdr *section_headers;
} Elf64_Headers;

// Reads elf header from a file to an elf_generic_header struct.
// Gets a file pointer and a pointer to elf_generic_header which it populates.
// It populates the bit_class, and the corresponding header type to the struct.
// It returns 0 on success and -1 on error.
int read_elf_header(FILE *fp, Elf64_Headers *elf);

// Reads the elf program headers from an elf file to an elf_generic_header
// struct (that already has a bit_class and elf_header). Gets a file pointer and
// a pointer to elf_generic_header which it populates. It mallocs and populates
// the program headers to the struct. It returns 0 on success and -1 on error.
int read_elf_program_headers(FILE *fp, Elf64_Headers *elf);

// Reads the elf section headers from an elf file to an elf_generic_header
// struct (that already has a bit_class and elf_header). Gets a file pointer and
// a pointer to elf_generic_header which it populates. It mallocs and populates
// the section headers to the struct. It returns 0 on success and -1 on error.
int read_elf_section_headers(FILE *fp, Elf64_Headers *elf);

#endif
