#include "crc32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    if (crc32("123456789",9) != 0xcbf43926u || crc32(NULL,0) != 0) return 1;
    if (argc != 2) return 2;
    FILE *file = fopen(argv[1],"rb");
    if (!file) return 3;
    uint8_t block[257];
    uint32_t state=UINT32_MAX;
    size_t count;
    while ((count=fread(block,1,sizeof(block),file))) state=crc32_update(state,block,count);
    if (ferror(file)) { fclose(file); return 4; }
    fclose(file);
    printf("%08x\n", (unsigned)(state ^ UINT32_MAX));
    return 0;
}
