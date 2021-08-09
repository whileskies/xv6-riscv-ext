#include "types.h"
#include "riscv.h"
#include "defs.h"

#define isascii(x) ((x >= 0x00) && (x <= 0x7f))
#define isprint(x) ((x >= 0x20) && (x <= 0x7e))

void
hexdump (void *data, uint size) {
  int offset, index;
  unsigned char *src;

  src = (unsigned char *)data;
  printf("+------+-------------------------------------------------+------------------+\n");
  for (offset = 0; offset < size; offset += 16) {
    printf("| ");
    if (offset <= 0x0fff) printf("0");
    if (offset <= 0x00ff) printf("0");
    if (offset <= 0x000f) printf("0");
    printf("%x | ", offset);
    for (index = 0; index < 16; index++) {
      if(offset + index < (int)size) {
        if (src[offset + index] <= 0x0f) printf("0");
        printf("%x ", 0xff & src[offset + index]);
      } else {
        printf("   ");
      }
    }
    printf("| ");
    for(index = 0; index < 16; index++) {
      if(offset + index < (int)size) {
        if(isascii(src[offset + index]) && isprint(src[offset + index])) {
          printf("%c", src[offset + index]);
        } else {
          printf(".");
        }
      } else {
        printf(" ");
      }
    }
    printf(" |\n");
  }
  printf("+------+-------------------------------------------------+------------------+\n");
}