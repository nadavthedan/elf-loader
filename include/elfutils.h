#ifndef ELFUTILS_H
#define ELFUTILS_H

#include <elf.h>
#include <stdint.h>
#include <stdio.h>

// A type wiht the class of an elf header (32 bit or 64 bit)
// and a union of the header itself which shuld be used based on the bit_class.
typedef struct {
  uint8_t bit_class;
  union {
    Elf32_Ehdr h32;
    Elf64_Ehdr h64;
  } elf_header;
  union {
    Elf32_Phdr *h32;
    Elf64_Phdr *h64;
  } program_headers;
  union {
    Elf32_Shdr *h32;
    Elf64_Shdr *h64;
  } section_headers;
} elf_generic_headers;

// Reads elf header from a file to an elf_generic_header struct.
// Gets a file pointer and a pointer to elf_generic_header which it populates.
// It populates the bit_class, and the corresponding header type to the struct.
// It returns 0 on success and -1 on error.
int read_elf_header(FILE *fp, elf_generic_headers *elf);

// Reads the elf program headers from an elf file to an elf_generic_header
// struct (that already has a bit_class and elf_header). Gets a file pointer and
// a pointer to elf_generic_header which it populates. It mallocs and populates
// the program headers to the struct. It returns 0 on success and -1 on error.
int read_elf_program_headers(FILE *fp, elf_generic_headers *elf);

// Reads the elf section headers from an elf file to an elf_generic_header
// struct (that already has a bit_class and elf_header). Gets a file pointer and
// a pointer to elf_generic_header which it populates. It mallocs and populates
// the section headers to the struct. It returns 0 on success and -1 on error.
int read_elf_section_headers(FILE *fp, elf_generic_headers *elf);

#endif
