// Demonstrates printBinaryHex()/printHex() (src/binary_hex.h) -- new
// utility functions for inspecting a compiled script's raw output as a
// hex dump, since neither this port nor upstream v1 (ESPLiveScript) had
// one (see binary_hex.h's header comment for what v1's closest relative,
// asm_parser.h's printparsdAsm(), actually does instead -- disassembly,
// not a raw byte dump, and unreachable outside a commented-out debug
// guard).
//
// Uses the manual parse -> createBinary() pipeline (SaveScriptBinary.ino's
// style), not parseScript()/ScriptExecutable -- the latter only ever
// hands back a loaded `executable`, already consumed from the
// intermediate `Binary` this example needs direct access to.
//
// printBinaryHex(&bin) prints two labeled hex dumps:
//   - instructions: the actual compiled machine code (bin.binary_data,
//     bin.instruction_size bytes) -- destined for IRAM on a real board.
//   - relocation header: bin.function_data (bin.function_size bytes),
//     decoded at load time by createExecutableFromBinary() to patch call
//     sites and reserve each declared function's entry point. Its bytes
//     are a mix of small binary fields and embedded, NUL-terminated
//     name strings (e.g. "@_fact(num)") -- readable directly in the hex
//     dump if you know to look for the ASCII range.
//
// printHex() itself is generic -- also demonstrated here on a small
// buffer that has nothing to do with a compiled script, to show it's a
// general byte-dump utility, not something tied to Binary specifically.
#include "parser.h"
#include "compiler_error.h"
#include "asm_parser.h"
#include "asm_serialize.h"
#include "binary_hex.h"

char script[] = R"EOF(
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

void setup()
{
   Serial.begin(115200);

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

void loop()
{
}
