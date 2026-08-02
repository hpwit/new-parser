#include "binary_hex.h"
#include <cstdio>

void printHex(const uint8_t *data, uint32_t size, uint32_t bytesPerLine)
{
    if (data == NULL || size == 0)
    {
        printf("(empty)\n");
        return;
    }
    if (bytesPerLine == 0)
        bytesPerLine = 16;

    for (uint32_t offset = 0; offset < size; offset += bytesPerLine)
    {
        printf("%06x: ", offset);
        uint32_t lineEnd = offset + bytesPerLine;
        if (lineEnd > size)
            lineEnd = size;
        for (uint32_t i = offset; i < lineEnd; i++)
            printf("%02x ", data[i]);
        printf("\n");
    }
}

void printBinaryHex(Binary *bin)
{
    if (bin == NULL || bin->error.error)
        return;

    printf("instructions (%u bytes):\n", bin->instruction_size);
    printHex(bin->binary_data, bin->instruction_size);

    printf("relocation header (%u bytes):\n", bin->function_size);
    printHex(bin->function_data, bin->function_size);
}
