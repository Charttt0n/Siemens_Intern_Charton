#include "image.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    static uint8_t memory[FLASH_SIZE];
    flash_device dev = {0};
    image_info info = {0};
    if (argc != 3) return 2;
    FILE *file=fopen(argv[1],"rb");
    if (!file) return 3;
    size_t count=fread(memory,1,sizeof(memory),file);
    int extra=fgetc(file);
    fclose(file);
    if (count != FLASH_SIZE || extra != EOF) return 4;
    if (flash_sim_bind(&dev,memory,FLASH_SIZE)) return 5;
    int result=image_validate(&dev,&info);
    printf("IMAGE_RESULT=%d SIZE=%u\n",result,(unsigned)info.size);
    return result == atoi(argv[2]) ? 0 : 1;
}
