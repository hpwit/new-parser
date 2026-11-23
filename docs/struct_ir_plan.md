# The struct IR: plan

The compiler's intermediate form is lines of assembly text: `visitnode.cpp`
prints them (~540 `string_format` calls), the passes in `optimize.cpp` match
them back with `strcmp`/`sscanf`, and `asm_parser.cpp` re-parses every line
into `result_parse_line` before encoding. The `asm_optimzed` branch made the
text machinery fast (the pooled hash, the arenas, the label hash), but the
round trip itself remains: every instruction is formatted once and parsed
twice, and costs ~25 bytes of text plus a hash entry while it lives.

Replacing the text with a struct removes the formatting, both parses and
most of the per-line memory, and turns every optimizer pattern into integer
compares. Measured expectations, from profiling the current branch: another
3-5x on compile time, roughly half the remaining compile heap.

## The struct

One line becomes (12 bytes, in a flat `parsedLines`-style array):

    struct IRLine {
        uint16_t op;        // asmInstruction, plus label/pseudo kinds
        uint8_t  flags;     // label-target, no-cross, blanked
        uint8_t  nops;
        int32_t  v0, v1;    // registers or immediates, by the op's shape
        union { int32_t v2; uint32_t nameref; };   // nameref: asm_text, for
    };                      // labels, l32r targets, call names, .bytes

The operand shapes already exist as the `op_*` tables in `asm_encoders.h`;
the encoders (`bin_*`) take the same values array the struct holds. Text is
kept only as a *printer* (debugging, `display()`, inline asm input), never
as the working form.

## Staged migration (each stage compiles, tests green, committed)

1. **The emitter API.** `emit(op, a, b, c)` / `emitLabel(nameref)` on a new
   `IRBuffer`, plus `emitText(line)` which parses one line with the current
   `parseline()` (inline assembly, the few hand-written blocks like
   `_ffdiv[]`). `IRBuffer` replaces `Text` for content/footer only; `Text`
   stays for the tokenizer (`all_text`) and `asm_text`, which are string
   tables, not instruction streams.
2. **visitnode.** Mechanical: `bufferText->addAfter(string_format(asmInstr,
   r1, r2, r3))` becomes `buffer->emit(asmInstr, r1, r2, r3)`. The
   `string_format(asmInstructionsName[i], ...)` table maps 1:1 -- the enum
   is already in hand at every call site. The iterator dance
   (`putIteratorAtPos`, `sp` positions, `addAfter(pos, ...)`) maps to
   insert-at-index on the flat array; `blankCurrent()` sets the blanked
   flag instead of swapping in " ".
3. **optimize.** Each pass's `strncmp`/`sscanf` head becomes a switch on
   `op` and field reads. Port in this order (simplest first):
   `foldAddressScales`, `scheduleLoadUse`, Pass 1 reload-blanking,
   `optimizeSpeed`, `preserveAcrossCalls`, `promoteFrameSlots`,
   `allocateLoopRegisters` (`registerUseDef` becomes a table lookup on
   `op`). MiniXtensa in `test/host` grows an IR entry point, or interprets
   the printer's output -- the equivalence tests keep their meaning.
4. **The assembler.** `parseline()` collapses to: labels and pseudos as
   today, everything else straight to the encoder with the struct's values
   (the narrow-form choice moves with it). `splitOpcodeOperande`,
   `operandeParse` and the mnemonic chain remain only behind `emitText()`.
5. **Two-pass sizing** (now nearly free): a first walk sums real sizes, so
   `_instr_size`/`tmp_instr_size` stop being the `lines*3` estimate, data
   addresses are exact, and a `.bin` stops carrying ~15% of dead tail
   (wickwire today: 109 KB buffer for 63.6 KB of code -- the serialized
   file, the FFat copy and the load-time PSRAM copy all shrink).
6. **Cleanups unlocked:** the register allocator at emission (subsumes the
   post-hoc promotion passes), `LOOP`/`LOOPNEZ` for counted loops (sizes
   and nesting are visible in the IR), the single-expression inliner at the
   AST level feeding the emitter.

## Invariants to keep

- The fuzzer (`test/fuzz`, QEMU) and `test/host` must pass at every stage;
  binaries should stay bit-identical through stages 1-3 (the IR prints to
  the same text until stage 4 flips the assembler).
- `.bytes` payloads are binary (NULs) -- they keep the nameref/size path.
- Inline assembly functions still enter through the text parser.
- `asm_text`'s `nameref` is `int16_t`: widen to `int32_t` when the struct
  lands (a giant script's unique label texts could pass 32767).
