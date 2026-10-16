# Differential fuzzing

`fuzz.py` generates random programs in a subset of the script language
that also has a C meaning, and runs each one twice:

- compiled by asmparser2 and executed on an emulated ESP32 under QEMU
  (`compile_script.cpp` builds the compiler from `../../src`,
  `runner_fuzz.c` is the bare-metal runner);
- translated to C, compiled with the host compiler (`-fwrapv`) and run.

Both print a trace through an `emit()` host function, and `main()` ends by
emitting every global. Any difference is a compiler, assembler, optimizer
or loader bug, as is a crash or a hang.

Generated programs stay away from the places where the two languages
legitimately differ: `%` and `/` operands go through `nn()`/`dv()`, every
global is initialised before use (the script language does not zero
them), indexes go through `ix()` or are loop counters, and functions
called inside expressions have no side effects, so evaluation order does
not matter.

## Requirements

`XTENSA_GCC` and `QEMU` set as for `../qemu/run.sh`, plus a host C and
C++ compiler and Python 3.

## Commands

```sh
./fuzz.py --regressions          # the saved reproducers of past bugs (~1 min)
./fuzz.py --count 48             # smoke run: seeds 1..48 (~1-2 min with 8 cores)
./fuzz.py --count 1000 --start 5000 --jobs 8   # look for new bugs
./fuzz.py --print 41             # show the program a seed generates
./fuzz.py --reduce failures/seed_41.json       # shrink a failure
./fuzz.py --add-regression NAME failures/seed_41_reduced.json "what was wrong"
```

Failing programs are written to `failures/` (not committed) as `.sc`,
`.c` (the C reference), `.json` (the program itself) and `.txt` (the
difference).

## Regressions

`regressions/` holds one small program per bug found this way, each
reduced to the few lines that still show it. The `.json` file is the
test; the `.sc` next to it is the same program rendered for reading.
Programs are stored rather than seed numbers because a seed only means
the same program for as long as the generator does not change.

To check that a regression program really catches its bug, run it
against the sources from before the fix:

```sh
git archive <commit> asmparser2/src | tar -x -C /tmp/old
ASMPARSER_SRC=/tmp/old/asmparser2/src ./fuzz.py --regressions
```
