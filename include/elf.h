#include <stdint.h>
#define ELF_IDENT_MAGIC_NUMBER_LENGTH 4
#define ELF_IDENT_PADDING_LENGTH 7

typedef struct {
  uint8_t magicnumber[ELF_IDENT_MAGIC_NUMBER_LENGTH];
  uint8_t bitformat;
  uint8_t endianness;
  uint8_t version;
  uint8_t osabi;
  uint8_t osabiversion;
  uint8_t padding[ELF_IDENT_PADDING_LENGTH];
} __attribute__((packed)) elfheadident_t;

typedef struct {
  uint16_t filetype;
  uint16_t machine;
  uint32_t version;
  uint64_t entry;
  uint64_t programheader;
  uint64_t sectionheader;
  uint32_t flags;
  uint16_t headersize;
  uint16_t programheadersize;
  uint16_t programheadernum;
  uint16_t sectionheadersize;
  uint16_t sectionheadernum;
  uint16_t namessectionindex;
} __attribute__((packed)) elfhead_t_64;

typedef struct {
  elfheadident_t ident;
  uint16_t filetype;
  uint16_t machine;
  uint32_t version;
  uint32_t entry;
  uint32_t programheader;
  uint32_t sectionheader;
  uint32_t flags;
  uint16_t headersize;
  uint16_t programheadersize;
  uint16_t programheadernum;
  uint16_t sectionheadersize;
  uint16_t sectionheadernum;
  uint16_t namessectionindex;
} __attribute__((packed)) elfhead_t_32;
