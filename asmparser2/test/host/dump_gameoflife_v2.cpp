#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "parser.h"
#include "compiler_error.h"
#include "asm_parser.h"

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;

    std::ifstream sourceFile(argv[1]);
    std::string source((std::istreambuf_iterator<char>(sourceFile)), {});
    const std::string startMarker = "char script[] = R\"EOF(";
    const std::string endMarker = ")EOF\";";
    size_t start = source.find(startMarker);
    size_t end = source.find(endMarker, start);
    if (start == std::string::npos || end == std::string::npos)
        return 3;
    std::string script = "#define true 1\n#define false 0\nuint32_t _handle_;\n" +
                         source.substr(start + startMarker.size(), end - start - startMarker.size());

    Script input;
    input.addContent(strdup(script.c_str()));
    input.init();
    content.clear();
    header.clear();
    footer.clear();

    Parser parser;
    parser.clean();
    parser.parse(&input, &__allTokens);
    if (Error.error)
    {
        display_error(&Error);
        return 4;
    }

    Binary binary = createBinary(&footer, &header, &content, true);
    if (binary.error.error)
        return 5;
    fprintf(stderr, "METRICS %u %u %u\n", binary.instruction_size, binary.data_size, binary.tmp_instruction_size);
    return 0;
}
