# sc_examples pipeline verification

Verifies that the real-world `*.sc` example scripts shipped with the
sibling v1 repo (`asmparser/sc_examples/`, including `psion32/`) compile
and run correctly through asmparser2's pipeline: tokenize -> parse (AST)
-> codegen (`visitnode.cpp`) -> `optimize()` -> `createBinary()` (assembler)
-> `createExecutableFromBinary()` (loader/relocator). The test harness is
`test/host/test_sc_examples.cpp`; run it with:

```sh
cd test/host
make run-sc
```

It follows `test_parser.cpp`'s fork-per-test pattern (one script per forked
child, crash-isolated) but is a separate binary/`main()` (`build-sc/`,
`make run-sc`) so it doesn't disturb `test_parser.cpp`'s own suite.

## Scope: what "compiles and runs correctly" means here

There is no ESP32 hardware in this environment, and QEMU (Espressif's
Xtensa fork, see `test/qemu/README.md`) hangs with no output on *any*
float arithmetic/comparison instruction. `test/qemu/README.md` also
already documents that its verified envelope excludes scripts using
structs or 3+ functions. Every single script in this corpus uses floats,
structs, and/or several functions -- so **none of the 21 scripts fall
within any environment in this session that can execute real Xtensa code
and check a runtime result.** Real execution is architecturally
impossible to verify here for this corpus, not merely "not attempted."

`test/host/mini_xtensa.h`'s interpreter is similarly inapplicable: it
explicitly refuses `call8`/`callExt`, and every script here calls at
least one function.

So "runs correctly" is host-structurally-verified only, for all 21
scripts: parse succeeds, `createBinary()` (the assembler) succeeds and
produces non-empty instruction bytes, and `createExecutableFromBinary()`
(the loader/relocator) succeeds and reports at least one callable
function record, all with no crash and no assertion failure. This is
exactly what `test_parser.cpp`'s own `runGenerateBinaryTest`/
`runLoadAndRelocateTest` already treat as the ceiling of what a host build
can prove without real hardware.

## Harness-level script preprocessing

Three real quirks of the example corpus are normalized at the text level
in `preprocess()` (`test_sc_examples.cpp`) before compiling, so that
*the rest* of each script's syntax still gets exercised even where one
specific construct is out of scope. None of these change how the
compiler behaves -- they change what text the compiler is handed.

1. **Bare `define NAME val` (no leading `#`) -> `#define NAME val`.**
   Confirmed via a v1 host build (see below) that v1's tokenizer *also*
   rejects the bare form -- `tokenizer.h`'s `TokenKeywordDefine` handling
   only merges with a preceding `#` (`TokenDiese`); otherwise it becomes
   `TokenUnknown` and parsing fails immediately. v2's `tokenize.cpp` ported
   the identical logic. So bare `define` is invalid in *both* versions --
   a bug in the original example scripts (octo.sc, octo2.sc,
   squaresani.sc, Mandel320.sc, tetris.sc, scroll.sc), not a v2 gap.
   Normalized here purely to keep verifying the rest of those scripts.

2. **`import rand` stripped, `rand()` declared+bound as an ordinary
   external instead.** `import <name>` is real, v1-valid syntax: v1's
   tokenizer (`tokenizer.h`) special-cases `import <stdlib name>` by
   splicing in a real `__ASM__` implementation of that name from
   `functionlib.h`'s `_stdlib` table (`sync`/`rand`/`copy`/`memset`/
   `fill`/`arduino`) in place of the two tokens, confirmed by reading
   v1's tokenizer directly and by a v1 host build successfully parsing
   `balls.sc`/`ballsbw.sc`/`ballscardputer.sc` as-is. **v2's
   `tokenize.cpp` has the identical mechanism sketched but left
   commented-out** (references an undefined `findLibFunction()`/
   `_stdlib`, right after the working `TokenKeywordDefine` handling) --
   `import rand` therefore fails to parse in v2 (`TokenKeywordImport`
   reaches `parseType()` and errors). This is a real, root-caused, but
   **intentionally-not-fixed-here** porting gap: safely reviving
   character-stream splicing mid-tokenization (matching all six stdlib
   entries, not just `rand`) is a bigger, riskier change than this
   session's other fixes, and only `rand` is actually exercised by this
   corpus. Worked around at the harness level by stripping `import rand`
   and registering `rand()` as an ordinary bound external, so the rest of
   balls.sc/ballsbw.sc/ballscardputer.sc/tetris.sc (struct arrays, ball
   physics, tetris board logic) still gets verified.

3. **Always-prepended prelude**: `#define true 1`, `#define false 0`,
   `uint32_t _handle_;`, `uint32_t _execaddr_;`, and an
   `external uint32_t rand(uint32_t mod);` declaration. v1's real compile
   entrypoints (`ESPLiveScript.h`'s `parseScript()`/`parseScriptBinary()`
   etc.) *always* prepend a `_sync`/`_syncExt`/`base_ext_functions`
   boilerplate before the user's script (four near-identical
   `addContent()` sequences, confirmed by reading `ESPLiveScript.h:
   303-356`) -- which is where `true`/`false` and the `_handle_`/
   `_execaddr_` globals real scripts silently depend on actually come
   from. v2's `Parser::parse()` is the raw, low-level entrypoint (same one
   `test_parser.cpp` calls directly) and has no higher-level "compile a
   user script" wrapper yet to attach this policy to, so it isn't done
   automatically -- a caller has to do it, same as this harness does here.
   `sync()` itself is registered as an ordinary `bindFunction()` stub
   (see `registerCommonBindings()`) rather than reimplemented as v1's
   raw-asm trampoline, since v1's own `sync()` implementation just
   `callExt`s into a host-bound `sync` anyway -- behaviorally equivalent
   for structural verification.

## v2 compiler bugs found and fixed this session

Found while getting this corpus to compile; all five are real,
root-caused, and have regression tests in `test/host/test_parser.cpp`.

1. **Auto-declared (`bindFunction()`/`bindVariable()`-only, no `external`
   line) calls/variables never reached codegen.**
   `src/parser.cpp`'s `parseFunctionCall()` and `getVariable()` parse a
   synthesized declaration (built from the matching `binded_assets` entry)
   when a script uses a name with no `external` line of its own. That
   declaration used to be parsed into `extra_parser` -- a scratch
   `NodeToken` that's never a descendant of `program` -- so it was
   correctly registered for *name lookup* (`functions`/`main_context`,
   which is why parsing itself succeeded) but `program.visitNode()` never
   walked it, so its jump-table header reservation was silently dropped
   and `createBinary()` either errored or produced a binary the loader
   rejected. Fixed by parsing straight into `program` in both sites
   instead. This is the single highest-impact fix in this session: it's
   what makes circles.sc, dsiplayicon.sc, scroll.sc, octo2.sc, and the
   `balls`/`ballsbw`/`ballscardputer` family (all of which call functions
   and/or use `leds` with zero `external` declarations) reach
   `createExecutableFromBinary()` at all. Regression test:
   `runAutoDeclareBinaryTest()` in `test_parser.cpp`.
   (`src/parser.cpp`, `parseFunctionCall()` ~line 1117,
   `getVariable()` ~line 2630.)

2. **Unsigned comparisons (`uint32_t`) failed to assemble
   ("Opcode bltu not found").**
   `src/visitnode.cpp`'s comparison codegen emits `blt`/`bge` with a `u`
   suffix (`asmInstructionsName[]`'s `"blt%s ..."`/`"bge%s ..."` entries)
   whenever either compared operand is `uint32_t`. `src/asm_parser.cpp`'s
   opcode dispatch only ever recognized the bare signed `"blt"`/`"bge"`
   strings -- `bin_bltu()`/`bin_bgeu()` (`src/asm_encoders.h`) already
   correctly encode the real Xtensa BLTU/BGEU opcodes, they just weren't
   wired into the dispatch. Any script comparing a `uint32_t` (e.g.
   ballscardputer.sc's `while (h < 700)` with `uint32_t h`) failed to
   assemble. Fixed by adding `"bltu"`/`"bgeu"` dispatch entries alongside
   `"blt"`/`"bge"`. Regression test: "generates executable binary:
   unsigned (uint32_t) comparison uses bltu" in `test_parser.cpp`.
   (`src/asm_parser.cpp`, ~line 442-490.)

3. **A script with 2+ `__ASM__` functions sharing an identical
   instruction line corrupted that line on the 2nd/3rd occurrence.**
   `src/visitnode.cpp`'s `_visitstringNode()` (strips the surrounding
   quotes off each `__ASM__` instruction-line string literal) used to do
   it with an in-place `memmove()` on `nd->getText()`'s own buffer.
   `Text::addText()` (`src/stackfunctions.cpp`) deduplicates/interns
   identical string literals to the same underlying allocation, so a
   second `__ASM__` function repeating a line verbatim from an earlier
   one (extremely common -- e.g. `"entry a1,32"` and `"retw.n"` show up in
   nearly every `__ASM__` function) shared that exact buffer, and got it
   stripped a second time: one more character eaten off each end per
   repeat (`"entry a1,32"` -> `"ntry a1,3"` -> `"try a1,"` the 3rd time),
   which the assembler then correctly rejected as unrecognized opcodes
   ("Opcode ntry not found", "Opcode 32r not found", ...). This is what
   broke squaresani.sc (three `__ASM__` functions --
   `setTime()`/`millis()`/`elapseMillis()` -- all sharing
   `"entry a1,32"`/`"retw.n"`) and would have broken any two-`__ASM__`
   -function script sharing so much as one line. Fixed by building a
   fresh stripped copy instead of mutating the (possibly-shared) source
   buffer. Regression test: "generates executable binary: two __ASM__
   functions sharing an identical instruction line" in `test_parser.cpp`.
   (`src/visitnode.cpp`, `_visitstringNode()`.)

4. **`!expr` (logical not) used in more than one function in the same
   script failed to assemble ("label label_not_999 is already
   declared").**
   `src/visitnode.cpp`'s `TokenNot` codegen labels its branch target
   `"label_not_<for_if_num2>"`, where `for_if_num2` is a counter shared
   with two other codegen paths (`__test_safe_%d` array-bounds checks,
   `loop_label_%d`). Those other two increment it right after emitting
   their label so the next use gets a fresh number; the `TokenNot` site
   never did. So every `!` anywhere in a script reused the exact same
   label name -- harmless for a script with a single `!`, but a straight
   duplicate-label assembler error for any script using it in more than
   one place (tetris.sc calls `!checkCollision(...)` in `left()`,
   `right()`, and `main()`'s loop). Fixed by adding the same
   `for_if_num2++` the other three sites already have. Regression test:
   "generates executable binary: '!' operator used in two different
   functions gets distinct labels" in `test_parser.cpp`.
   (`src/visitnode.cpp`, `TokenNot` handling ~line 258-271.)

5. **A stray `;` after a struct definition, then a global of that type,
   crashed the parser (segfault) instead of erroring.**
   `struct Foo {...};` (trailing `;`, a natural C habit -- invalid in
   this language in both v1 and v2) followed by a global
   `Foo instance[N];` left the declared variable's type unresolved
   (`_vartype` stuck at `EOF_VARTYPE`) instead of erroring out
   immediately. `copyPrty()` (`src/nodetoken.cpp`) then unconditionally
   dereferenced `getVarTypeObj()`'s result, which is `NULL` for an
   unresolved type -- SIGSEGV. Confirmed via a v1 host build that v1
   handles the *identical* input cleanly (its own "expected identifier"
   parse error, no crash) -- so this was a v2-only robustness regression,
   not a case v1 also mishandles. Found via `psion32/snake.sc`, whose
   very first real construct is exactly this pattern
   (`struct Segment {...};` then `Segment snake[MAX_SNAKE];`). Fixed by
   having `copyPrty()` report a clean parse error
   (`impossibletofindvariabledeclaration`) instead of crashing when the
   source type doesn't resolve; some other check further along in
   `parseProgram()` typically ends up reporting a (different, also valid)
   error first once the crash no longer masks it -- the fix's actual goal
   is "never crash, always report *some* real parse error", not a
   specific error code. Regression test: "struct definition with stray
   trailing ';' then a global of that type (must not crash)" in
   `test_parser.cpp`. (`src/nodetoken.cpp`, `copyPrty()`.)

## v1 ground truth checks

Several open questions from the initial syntax survey were resolved by
building v1's `ESPLiveScript.h` standalone on host
(`-D__TEST_DEBUG -D__COMPILER_TEST`, stubbing only
`esp_get_free_heap_size()`) and calling `Parser::parse()` directly (going
further into `compile()`/`compileBinary()`'s `createExectutable()`
dereferences real ESP32 addresses and segfaults on host -- expected, that
codepath is exactly what asmparser2 exists to make host-testable). This
was a throwaway host build, not committed anywhere in either repo.

- **`sizex`/`sizey` in Madel320.sc/Mandel320.sc**: confirmed v1 also
  rejects these scripts ("impossible to find declaraiton for sizex") --
  they're assigned in `main()` with no declaration anywhere in either
  version's runtime or the script itself. A bug in the original example,
  not a v2 porting gap.
- **`typedef struct {...} Name;` in psion32/arkanoid.sc**: confirmed v1's
  `parseProgram()` has no `typedef` handling at all (only
  `struct NAME {...}`) -- v1 error: "expecting external, __ASM__ or
  variable type ... typedef". Not this DSL's syntax in either version.
  (arkanoid.sc also relies on undeclared globals `led16ffer`/
  `framebuffer` and bodyless non-`external` hardware-abstraction
  prototypes -- further confirmation it's an aspirational/reference file,
  not a working script.)
- **psion32/snake.sc**: genuinely SDL2 C code (`SDL_Renderer*`,
  `switch`/`case`, `char *argv[]`), not this DSL at all. Fails immediately
  in v1 too (once past the crash described above, which v1 never hit in
  the first place since it errors instead of crashing on that first
  construct).
- **dsiplayicon.sc's `displaypic()`**: confirmed v1 rejects this script
  too ("function displaypic(...) not found"). `displaypic()` declares its
  pixel-map parameter as `char * pic`, but every call site passes a
  `uint8_t[]` array (`ghostp`/`mario`/`cerise`); both v1's and v2's
  `bindFunction()`-style type classifier treat `uint8_t` as `"num"`, not
  `"char"`, so the call-site signature never matches the declared one. A
  parameter-type bug in the original example script, not a v2 regression.
- **`import rand`**: confirmed v1-valid (see harness preprocessing item 2
  above) -- v1 parses balls.sc/ballsbw.sc/ballscardputer.sc unmodified.
- **Bare `define`**: confirmed v1-invalid too (see harness preprocessing
  item 1 above).
- **millis()/hsv() etc. via `bindFunction()`-only, no `external` line**:
  confirmed v1 parses circles.sc/fibonacci.sc (once the right host
  bindings are registered) without needing an explicit `external` line --
  v1 has its own (different, working) auto-declare mechanism for this,
  which is exactly what v2's fix #1 above brings to parity.

## Per-script status

| Script | Status | Notes |
|---|---|---|
| fibonacci.sc | PASS | Recursive `fib()`, `millis()` via bind. |
| Madel.sc | PASS | `#define`, `external CRGB leds[...]`, `hsv()`/`show()` via bind. |
| Madel320.sc | XFAIL (known v1 bug) | `sizex`/`sizey` undeclared implicit globals -- confirmed broken in v1 too. |
| Mandel320.sc | XFAIL (known v1 bug) | Same `sizex`/`sizey` bug, plus bare `define` for its first constant. |
| mandelBW.sc | PASS | `setPixel()`/`clear()`/`delay()` via bind, no `leds`. |
| octo.sc | PASS | Bare `define` normalized by harness; explicit `external`s otherwise fine. |
| octo2.sc | PASS | Bare `define` normalized; every external auto-declared (all commented out in source) -- exercises fix #1. |
| gamebw.sc | PASS | `int main()`, comma-form 2D array `copy[height,width]` also indexed flat (`copy[h]`). |
| gameoflife.sc | PASS | `external CRGB leds[...]`, `hsv()` auto-declared. |
| gameoflifecarputer.sc | PASS | `uint16_t leds`/`hsv()` -- separate binding set (`registerGameOfLifeCarputerBindings()`). |
| circles.sc | PASS | **Zero** `external` declarations at all -- fully exercises fix #1. |
| scroll.sc | PASS | Bare `define` normalized; `leds`/`hsv()`/`font` all auto-declared; `and` used as bitwise op. |
| dsiplayicon.sc | XFAIL (known v1 bug) | `displaypic(char* pic, ...)` called with `uint8_t[]` args -- confirmed broken in v1 too. |
| balls.sc | PASS | `import rand` stripped/rebound; struct array (`ball Balls[max_nb_balls]`), zero `external` decls. |
| ballscardputer.sc | PASS | Same as balls.sc; also exercises fix #2 (`uint32_t h` compared with `<`). |
| ballsbw.sc | PASS | Same as balls.sc, BW variant (`setPixel` instead of `leds`). |
| animwle.sc | PASS | Single `__ASM__ millis()`, `leds`/`hsv()` auto-declared, recursion-free helper functions named `fmod`/`triangle`/`time`. |
| squaresani.sc | PASS | Bare `define` normalized; three `__ASM__` functions sharing instruction lines -- exercises fix #3. |
| psion32/arkanoid.sc | XFAIL (out of scope) | `typedef struct`, bodyless non-`external` prototypes -- confirmed not valid in v1 either. |
| psion32/snake.sc | XFAIL (out of scope) | SDL2 C code, not this DSL. Its first real construct used to crash v2 (fix #4); now fails cleanly like v1. |
| tetris.sc | PASS | Bare `define`/`import rand` normalized by harness. `board[]` mixes comma-form (`board[y, x]`) and bracket-form (`board[y][x]`) 2D indexing of the *same* declared array -- both forms parse and assemble without error, so this is at worst a readability quirk in the original script, not something blocking compilation. Whether the two forms actually compute the *same* address (semantic/runtime correctness) is outside this session's structural-verification tier -- test/qemu/README.md's verified envelope excludes scripts with 3+ functions, and tetris.sc has well over a dozen. Also exercises fix #4 (`!checkCollision(...)` called from three different functions). |
| testjson.sc | XFAIL (trivial) | Empty file (0 bytes) -- nothing to compile. |

**16 PASS, 6 XFAIL (all confirmed known-broken-in-v1-too or genuinely
out of scope), 0 unexpected failures, 0 crashes.**
