#include <elf.h>
#include <stdint.h>

// A type wiht the class of an elf header (32 bit or 64 bit)
// and a union of the header itself which shuld be used based on the bit_class.
typedef struct {
  uint8_t bit_class;
  union {
    Elf32_Ehdr h32;
    Elf64_Ehdr h64;
  } header;
} elf_generic_header;

// Reads elf header from a file to an elf_generic_header struct.
// It populates the bit_class, and the corresponding header type to the struct.
// It returns 0 on success and -1 on error.
int read_elf_header(char *filename, elf_generic_header *header);
