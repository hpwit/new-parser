#define __TEST_DEBUG
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

uint32_t esp_get_free_heap_size() { return 0; }

#include "ESPLiveScript.h"

int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;

    std::ifstream sourceFile(argv[1]);
    std::string source((std::istreambuf_iterator<char>(sourceFile)), {});
    const std::string startMarker = "char script[] = R\"EOF(";
    const std::string endMarker = ")EOF\";";
    size_t start = source.find(startMarker);
    size_t end = source.find(endMarker, start);
    if (start == std::string::npos || end == std::string::npos)
        return 3;
    std::string script = source.substr(start + startMarker.size(), end - start - startMarker.size());

    main_script.clear();
    main_script.addContent((char *)_sync.c_str());
    main_script.addContent((char *)_syncExt.c_str());
    main_script.addContent((char *)base_ext_functions.c_str());
    main_script.addContent((char *)script.c_str());

    Parser parser;
    parser.parse();
    if (Error.error)
    {
        fprintf(stderr, "v1 parse failed: %s\n", Error.error_message.c_str());
        return 4;
    }
    buildParents(&program);
    program.visitNode();
    error_message_struct assemblyError = parseASM(&footer, &header, &content, &_asm_parsed);
    if (assemblyError.error)
        return 5;
    error_message_struct jumpError = calculateJump(tmp_exec, &_asm_parsed);
    if (jumpError.error)
        return 6;
    fprintf(stderr, "METRICS %u %u %u\n", _instr_size, _data_size, tmp_instr_size);

    if (freopen(argv[2], "w", stdout) == nullptr)
        return 7;
    printparsdAsm(0, &_asm_parsed);
    return 0;
}
