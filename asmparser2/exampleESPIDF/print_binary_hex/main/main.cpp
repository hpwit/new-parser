// ESP-IDF port of examples/PrintBinaryHex/PrintBinaryHex.ino --
// demonstrates printBinaryHex()/printHex() (src/binary_hex.h) for
// inspecting a compiled script's raw output as a hex dump.
//
// Uses the manual parse -> createBinary() pipeline (like
// SaveScriptBinary.ino/LanguageBasics.ino), not parseScript()/
// ScriptExecutable -- the latter only ever hands back a loaded
// `executable`, already consumed from the intermediate `Binary` this
// example needs direct access to.
#include "parser.h"
#include "compiler_error.h"
#include "asm_parser.h"
#include "asm_serialize.h"
#include "binary_hex.h"

static char script[] = R"EOF(
int fact(int h)
{
   if (h == 1)
   {
      return 1;
   }
   return h * fact(h - 1);
}

void main()
{
}
)EOF";

extern "C" void app_main(void)
{
   Script s;
   s.addContent(script);
   s.init();

   Parser p;
   p.clean();
   p.parse(&s, &__allTokens);

   if (Error.error)
   {
      display_error(&Error);
      return;
   }

   Binary bin = createBinary(&footer, &header, &content, false);
   if (bin.error.error)
   {
      printf("assembler error: %s\n", bin.error.error_message ? bin.error.error_message : "?");
      return;
   }

   printf("compiled '%s': %u instruction bytes, %u relocation-header bytes\n\n",
          "fact()/main()", bin.instruction_size, bin.function_size);
   printBinaryHex(&bin);

   printf("\nprintHex() is generic -- also works on any other buffer, e.g.\n"
          "a serializeBinary() blob (asm_serialize.h), or, just to show\n"
          "that here, an unrelated 12-byte buffer at 4 bytes/line:\n");
   uint8_t sample[12] = {0xDE, 0xAD, 0xBE, 0xEF, 1, 2, 3, 4, 5, 6, 7, 8};
   printHex(sample, sizeof(sample), 4);

   freeBinary(&bin);
}
