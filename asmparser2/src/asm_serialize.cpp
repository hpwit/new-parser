#include "asm_serialize.h"
#include <stdlib.h>
#include <string.h>

// Bytes 'S' 'C' 'P' '1' -- a format/version tag more than a real magic
// number, just enough to reject obviously-wrong or corrupt buffers
// before trusting their size fields.
#define SCRIPT_BINARY_MAGIC 0x31504353u

uint8_t *serializeBinary(Binary *bin, uint32_t *outSize)
{
    if (bin->error.error || bin->binary_data == NULL || bin->function_data == NULL)
        return NULL;

    uint32_t total = 4 + 2 * 4 + (uint32_t)bin->tmp_instruction_size + (uint32_t)bin->function_size;
    uint8_t *buf = (uint8_t *)malloc(total);
    uint8_t *p = buf;
    uint32_t magic = SCRIPT_BINARY_MAGIC;

    memcpy(p, &magic, 4);
    p += 4;
    memcpy(p, &bin->instruction_size, 2);
    p += 2;
    memcpy(p, &bin->tmp_instruction_size, 2);
    p += 2;
    memcpy(p, &bin->function_size, 2);
    p += 2;
    memcpy(p, &bin->data_size, 2);
    p += 2;
    memcpy(p, bin->binary_data, bin->tmp_instruction_size);
    p += bin->tmp_instruction_size;
    memcpy(p, bin->function_data, bin->function_size);
    p += bin->function_size;

    if (outSize != NULL)
        *outSize = total;
    return buf;
}

Binary deserializeBinary(const uint8_t *buf, uint32_t size)
{
    Binary bin;
    bin.binary_data = NULL;
    bin.function_data = NULL;
    bin.error.error = 0;
    bin.error.error_message = NULL;

    const uint32_t headerSize = 4 + 2 * 4;
    if (buf == NULL || size < headerSize)
    {
        bin.error.error = 1;
        bin.error.error_message = (char *)"serialized script binary is truncated";
        return bin;
    }

    const uint8_t *p = buf;
    uint32_t magic;
    memcpy(&magic, p, 4);
    p += 4;
    if (magic != SCRIPT_BINARY_MAGIC)
    {
        bin.error.error = 1;
        bin.error.error_message = (char *)"serialized script binary has the wrong format";
        return bin;
    }

    memcpy(&bin.instruction_size, p, 2);
    p += 2;
    memcpy(&bin.tmp_instruction_size, p, 2);
    p += 2;
    memcpy(&bin.function_size, p, 2);
    p += 2;
    memcpy(&bin.data_size, p, 2);
    p += 2;

    uint32_t expected = headerSize + (uint32_t)bin.tmp_instruction_size + (uint32_t)bin.function_size;
    if (size < expected)
    {
        bin.error.error = 1;
        bin.error.error_message = (char *)"serialized script binary is truncated";
        return bin;
    }

    bin.binary_data = (uint8_t *)malloc(bin.tmp_instruction_size);
    memcpy(bin.binary_data, p, bin.tmp_instruction_size);
    p += bin.tmp_instruction_size;

    bin.function_data = (uint8_t *)malloc(bin.function_size);
    memcpy(bin.function_data, p, bin.function_size);
    p += bin.function_size;

    return bin;
}

void freeBinary(Binary *bin)
{
    if (bin->binary_data != NULL)
    {
        free(bin->binary_data);
        bin->binary_data = NULL;
    }
    if (bin->function_data != NULL)
    {
        free(bin->function_data);
        bin->function_data = NULL;
    }
}
