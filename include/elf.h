#include <stdint.h>
#define ELF_MAGIC_NUMBER_LENGTH 4

typedef struct {
  unsigned char magicnumber[ELF_MAGIC_NUMBER_LENGTH];
  uint8_t bitformat;
  uint8_t endianness;
} elfhead_t;
