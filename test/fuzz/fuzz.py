#!/usr/bin/env python3
"""Differential fuzzer for the asmparser2 compiler.

Generates random programs in a subset of the script language that also
has a C meaning, then runs each one twice:

  * compiled by asmparser2 (compile_script.cpp, built from ../../src) and
    executed under QEMU on an emulated ESP32 (runner_fuzz.c);
  * translated to C, compiled with the host compiler (-fwrapv) and run.

Both print the same trace (one "EMIT <value>" line per emit() call, then
"DONE"); main() ends by emitting every global. Any difference is a bug in
the compiler, the assembler, the optimizer or the loader -- or a crash or
hang of the compiled code. Failing programs are saved, and can be
reduced to a small reproducer with --reduce.

Programs only use constructs whose meaning is the same in both
languages. In particular they avoid, by construction:
  * division by zero, and INT_MIN / -1 (divisors go through dv());
  * reading uninitialised globals (main() initialises everything first;
    the script language does not zero them);
  * out-of-bounds indexes (they go through ix() or are loop counters);
  * order-of-evaluation differences: functions called inside expressions
    are pure (no emit(), no global writes); functions with side effects
    are only called as whole statements.

Usage:
  ./fuzz.py --regressions                   # the saved reproducers of past bugs
  ./fuzz.py --count 48                      # smoke run: seeds 1..48
  ./fuzz.py --count 200 --jobs 8            # run 200 random programs
  ./fuzz.py --seed 1234                     # one specific program
  ./fuzz.py --reduce failures/seed_1234.sc  # shrink a saved failure
  ./fuzz.py --add-regression NAME failures/seed_1234_reduced.json "what broke"
                                            # keep a reduced failure as a regression
Needs XTENSA_GCC and QEMU (see ../qemu/README.md), and a host C++ and C
compiler.
"""

import argparse
import concurrent.futures
import os
import random
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
# ASMPARSER_SRC points at another copy of the compiler sources, e.g. an
# older commit, to check that a regression program really catches its bug.
SRC = os.environ.get("ASMPARSER_SRC") or os.path.normpath(os.path.join(HERE, "..", "..", "src"))
# One build directory per source tree: the rebuild check below compares
# timestamps, which says nothing when switching to a different tree.
import hashlib
BUILD = os.path.join(HERE, "build", hashlib.sha1(os.path.abspath(SRC).encode()).hexdigest()[:10])
REGRESSIONS = os.path.join(HERE, "regressions")
FAILURES = os.path.join(HERE, "failures")

# ---------------------------------------------------------------------------
# Program model
#
# Expressions are tuples:
#   ("lit", n) ("var", name) ("idx", array, [index...])
#   ("bin", op, a, b)         op in + - * / %
#   ("tern", cond, a, b)
#   ("call", name, [args])
# Conditions:
#   ("cmp", op, a, b) ("and", c1, c2) ("or", c1, c2)
# Statements are dicts with a "kind" key; blocks are lists of statements.
# ---------------------------------------------------------------------------

ARRAYS = {
    # name: (element type, dimensions)
    "A": ("int", [8]),
    "B": ("uint8_t", [6]),
    "M": ("int", [3, 4]),
    "N": ("uint8_t", [4, 5]),
}
SCALARS = ["g0", "g1", "g2"]

# Helper functions present in every program, identical in both languages.
HELPERS = """int nn(int v)
{
   if (v < 0)
      v = 0 - v;
   if (v < 0)
      v = 0;
   return v;
}

int ix(int v, int n)
{
   return nn(v) % n;
}

int dv(int v)
{
   return nn(v) % 7 + 1;
}
"""

SCRIPT_PRELUDE = """external void emit(int v);
external int ext1(int v);
external int ext2(int a, int b);
"""

C_PRELUDE = """#include <stdio.h>
#include <stdint.h>
// Must match runner_fuzz.c's host functions.
static void emit(int v) { printf("EMIT %d\\n", v); }
static int ext1(int v) { return v * 3 + 1; }
static int ext2(int a, int b) { return a - 2 * b; }
"""


class Gen:
    """Builds one random program from a seed."""

    def __init__(self, seed):
        self.r = random.Random(seed)
        self.funcs = []  # dicts: name, params, body, pure

    # -- expressions -------------------------------------------------------

    def literal(self):
        r = self.r.random()
        if r < 0.45:
            return ("lit", self.r.randint(0, 20))
        if r < 0.6:
            return ("lit", self.r.choice([2, 3, 4, 5, 8, 9, 16, 127, 128, 255, 256]))
        if r < 0.75:
            return ("bin", "-", ("lit", 0), ("lit", self.r.randint(1, 300)))
        if r < 0.9:
            return ("lit", self.r.randint(100, 2047))
        return ("lit", self.r.randint(2048, 200000))

    def index_for(self, dim, scope, depth):
        loops = [v for v, n in scope["loops"] if n <= dim]
        r = self.r.random()
        if loops and r < 0.4:
            return ("var", self.r.choice(loops))
        if r < 0.6:
            return ("lit", self.r.randint(0, dim - 1))
        return ("call", "ix", [self.expr(scope, depth + 1), ("lit", dim)])

    def array_read(self, scope, depth):
        name = self.r.choice(list(ARRAYS))
        dims = ARRAYS[name][1]
        return ("idx", name, [self.index_for(d, scope, depth) for d in dims])

    def pure_call(self, scope, depth):
        pure = [f for f in self.funcs if f["pure"]]
        r = self.r.random()
        if pure and r < 0.5:
            f = self.r.choice(pure)
            return ("call", f["name"], [self.expr(scope, depth + 1) for _ in f["params"]])
        if r < 0.75:
            return ("call", "ext1", [self.expr(scope, depth + 1)])
        return ("call", "ext2", [self.expr(scope, depth + 1), self.expr(scope, depth + 1)])

    def expr(self, scope, depth=0):
        variables = scope["vars"] + [v for v, _ in scope["loops"]] + SCALARS
        if depth >= 3:
            choice = self.r.choice(["lit", "var", "var"])
        else:
            choice = self.r.choices(
                ["lit", "var", "arr", "bin", "bin", "tern", "call"],
                weights=[2, 3, 2, 4, 0, 1, 2])[0]
        if choice == "lit":
            return self.literal()
        if choice == "var":
            return ("var", self.r.choice(variables))
        if choice == "arr":
            return self.array_read(scope, depth)
        if choice == "tern":
            return ("tern", self.cond(scope, depth + 1), self.expr(scope, depth + 1), self.expr(scope, depth + 1))
        if choice == "call":
            return self.pure_call(scope, depth)
        op = self.r.choice(["+", "+", "-", "*", "/", "%"])
        a = self.expr(scope, depth + 1)
        b = self.expr(scope, depth + 1)
        if op == "/":
            return ("bin", "/", a, ("call", "dv", [b]))
        if op == "%":
            # Signed remainder, as in C: the left operand may be negative,
            # and so may the divisor -- never 0 and never -1 (INT_MIN % -1
            # is undefined in C).
            divisor = ("call", "dv", [b])
            if self.r.random() < 0.3:
                divisor = ("bin", "-", ("lit", 0), ("bin", "+", divisor, ("lit", 1)))
            left = a if self.r.random() < 0.6 else ("call", "nn", [a])
            return ("bin", "%", left, divisor)
        return ("bin", op, a, b)

    def cond(self, scope, depth=0):
        r = self.r.random()
        if depth < 2 and r < 0.15:
            return ("and", self.cond(scope, depth + 1), self.cond(scope, depth + 1))
        if depth < 2 and r < 0.3:
            return ("or", self.cond(scope, depth + 1), self.cond(scope, depth + 1))
        op = self.r.choice(["<", "<=", ">", ">=", "==", "!="])
        return ("cmp", op, self.expr(scope, depth + 1), self.expr(scope, depth + 1))

    # -- statements ------------------------------------------------------------

    def new_local(self, scope):
        scope["counter"][0] += 1
        return "l%d" % scope["counter"][0]

    def block(self, scope, n, depth, pure):
        inner = {"vars": list(scope["vars"]), "loops": list(scope["loops"]), "counter": scope["counter"]}
        out = []
        for _ in range(n):
            out.append(self.stmt(inner, depth, pure))
        return out

    def stmt(self, scope, depth, pure):
        kinds = ["decl", "assign", "assign", "if", "for"]
        weights = [3, 4, 2, 2, 2]
        if not pure:
            kinds += ["store", "store", "emit", "gassign", "effectcall"]
            weights += [4, 2, 3, 2, 1]
        if depth >= 3:
            weights = [w if k not in ("if", "for") else 0 for k, w in zip(kinds, weights)]
        kind = self.r.choices(kinds, weights=weights)[0]

        if kind == "decl" or (kind == "assign" and not scope["vars"]):
            name = self.new_local(scope)
            e = self.expr(scope)
            scope["vars"].append(name)
            return {"kind": "decl", "name": name, "expr": e}
        if kind == "assign":
            return {"kind": "assign", "target": ("var", self.r.choice(scope["vars"])),
                    "op": self.r.choice(["=", "=", "+=", "-="]), "expr": self.expr(scope)}
        if kind == "gassign":
            return {"kind": "assign", "target": ("var", self.r.choice(SCALARS)),
                    "op": self.r.choice(["=", "+=", "-="]), "expr": self.expr(scope)}
        if kind == "store":
            target = self.array_read(scope, 0)
            return {"kind": "assign", "target": target, "op": "=", "expr": self.expr(scope)}
        if kind == "emit":
            return {"kind": "emit", "expr": self.expr(scope)}
        if kind == "effectcall":
            effect = [f for f in self.funcs if not f["pure"]]
            if not effect:
                return {"kind": "emit", "expr": self.expr(scope)}
            f = self.r.choice(effect)
            args = [self.expr(scope) for _ in f["params"]]
            if scope["vars"] and self.r.random() < 0.7:
                return {"kind": "assign", "target": ("var", self.r.choice(scope["vars"])), "op": "=",
                        "expr": ("call", f["name"], args)}
            return {"kind": "callstmt", "call": ("call", f["name"], args)}
        if kind == "if":
            branches = [(self.cond(scope), self.block(scope, self.r.randint(1, 3), depth + 1, pure))]
            while self.r.random() < 0.4 and len(branches) < 4:
                branches.append((self.cond(scope), self.block(scope, self.r.randint(1, 3), depth + 1, pure)))
            other = self.block(scope, self.r.randint(1, 2), depth + 1, pure) if self.r.random() < 0.6 else None
            return {"kind": "if", "branches": branches, "else": other, "braces": self.r.random() < 0.6}
        # for
        scope["counter"][0] += 1
        var = "i%d" % scope["counter"][0]
        count = self.r.randint(1, 6)
        inner = {"vars": list(scope["vars"]), "loops": scope["loops"] + [(var, count)], "counter": scope["counter"]}
        body = [self.stmt(inner, depth + 1, pure) for _ in range(self.r.randint(1, 3))]
        return {"kind": "for", "var": var, "count": count, "body": body}

    # -- program ---------------------------------------------------------------

    def program(self):
        nfuncs = self.r.randint(1, 5)
        for i in range(nfuncs):
            pure = self.r.random() < 0.5
            # Mostly 0-3 parameters, sometimes 4-6: the fifth and sixth go
            # through the stack, and six reach every argument register.
            nparams = self.r.choices(range(7), weights=[2, 3, 3, 3, 1, 1, 1])[0]
            params = ["p%d" % k for k in range(nparams)]
            scope = {"vars": list(params), "loops": [], "counter": [0]}
            body = self.block(scope, self.r.randint(1, 5), 0, pure)
            ret = self.expr({"vars": scope["vars"] + params, "loops": [], "counter": scope["counter"]})
            self.funcs.append({"name": "f%d" % i, "params": params, "body": body, "ret": ret, "pure": pure})
        scope = {"vars": [], "loops": [], "counter": [0]}
        main = self.block(scope, self.r.randint(3, 10), 0, False)
        return {"funcs": self.funcs, "main": main}


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------

def render_expr(e, c):
    k = e[0]
    if k == "lit":
        return str(e[1])
    if k == "var":
        return e[1]
    if k == "idx":
        if c:
            return e[1] + "".join("[%s]" % render_expr(i, c) for i in e[2])
        return "%s[%s]" % (e[1], ", ".join(render_expr(i, c) for i in e[2]))
    if k == "bin":
        return "(%s %s %s)" % (render_expr(e[2], c), e[1], render_expr(e[3], c))
    if k == "tern":
        return "(%s ? %s : %s)" % (render_cond(e[1], c), render_expr(e[2], c), render_expr(e[3], c))
    if k == "call":
        return "%s(%s)" % (e[1], ", ".join(render_expr(a, c) for a in e[2]))
    raise ValueError(k)


def render_cond(e, c):
    if e[0] == "cmp":
        return "%s %s %s" % (render_expr(e[2], c), e[1], render_expr(e[3], c))
    op = "&&" if e[0] == "and" else "||"
    return "(%s) %s (%s)" % (render_cond(e[1], c), op, render_cond(e[2], c))


def render_block(stmts, c, ind):
    out = []
    for s in stmts:
        out.extend(render_stmt(s, c, ind))
    return out


def render_stmt(s, c, ind):
    p = "   " * ind
    k = s["kind"]
    if k == "decl":
        return ["%sint %s = %s;" % (p, s["name"], render_expr(s["expr"], c))]
    if k == "assign":
        return ["%s%s %s %s;" % (p, render_expr(s["target"], c), s["op"], render_expr(s["expr"], c))]
    if k == "emit":
        return ["%semit(%s);" % (p, render_expr(s["expr"], c))]
    if k == "callstmt":
        return ["%s%s;" % (p, render_expr(s["call"], c))]
    if k == "for":
        out = ["%sfor (int %s = 0; %s < %d; %s++)" % (p, s["var"], s["var"], s["count"], s["var"]), p + "{"]
        out += render_block(s["body"], c, ind + 1)
        return out + [p + "}"]
    if k == "if":
        out = []
        for n, (cond, body) in enumerate(s["branches"]):
            head = "if" if n == 0 else "else if"
            out.append("%s%s (%s)" % (p, head, render_cond(cond, c)))
            out += braced(body, c, ind, s["braces"])
        if s["else"] is not None:
            out.append(p + "else")
            out += braced(s["else"], c, ind, s["braces"])
        return out
    raise ValueError(k)


def braced(body, c, ind, braces):
    p = "   " * ind
    # A body that declares a local always needs its own block; so does a
    # body of more than one statement.
    if braces or len(body) != 1 or body[0]["kind"] == "decl":
        return [p + "{"] + render_block(body, c, ind + 1) + [p + "}"]
    return render_block(body, c, ind + 1)


def render(prog, c):
    out = [C_PRELUDE if c else SCRIPT_PRELUDE]
    for name, (typ, dims) in ARRAYS.items():
        if c:
            out.append("%s %s%s;" % (typ, name, "".join("[%d]" % d for d in dims)))
        else:
            out.append("%s %s[%s];" % (typ, name, ", ".join(str(d) for d in dims)))
    for g in SCALARS:
        out.append("int %s;" % g)
    out.append("")
    out.append(HELPERS)
    for f in prog["funcs"]:
        out.append("int %s(%s)" % (f["name"], ", ".join("int " + x for x in f["params"])))
        out.append("{")
        out += render_block(f["body"], c, 1)
        out.append("   return %s;" % render_expr(f["ret"], c))
        out.append("}")
        out.append("")
    out.append("int main()" if c else "void main()")
    out.append("{")
    # Initialise every global: the script language does not zero them.
    for g, v in zip(SCALARS, (11, 22, 33)):
        out.append("   %s = %d;" % (g, v))
    for name, (typ, dims) in ARRAYS.items():
        loops = ["z%d" % k for k in range(len(dims))]
        for depth, (v, d) in enumerate(zip(loops, dims)):
            out.append("   " * (depth + 1) + "for (int %s = 0; %s < %d; %s++)" % (v, v, d, v))
        target = ("idx", name, [("var", v) for v in loops])
        value = ("bin", "+", ("bin", "*", ("var", loops[-1]), ("lit", 7)), ("lit", len(name) + 3))
        out.append("   " * (len(dims) + 1) + "%s = %s;" % (render_expr(target, c), render_expr(value, c)))
    out += render_block(prog["main"], c, 1)
    # Dump every global so memory corruption shows up too.
    for g in SCALARS:
        out.append("   emit(%s);" % g)
    for name, (typ, dims) in ARRAYS.items():
        loops = ["z%d" % k for k in range(len(dims))]
        for depth, (v, d) in enumerate(zip(loops, dims)):
            out.append("   " * (depth + 1) + "for (int %s = 0; %s < %d; %s++)" % (v, v, d, v))
        out.append("   " * (len(dims) + 1) + "emit(%s);" % render_expr(("idx", name, [("var", v) for v in loops]), c))
    if c:
        out.append('   printf("DONE\\n");')
        out.append("   return 0;")
    out.append("}")
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# Running
# ---------------------------------------------------------------------------

def tool_paths():
    gcc = os.environ.get("XTENSA_GCC")
    qemu = os.environ.get("QEMU")
    if not gcc or not qemu:
        sys.exit("set XTENSA_GCC and QEMU (see test/qemu/README.md)")
    return gcc, qemu


def build_compiler():
    """Builds compile_script against the current compiler sources."""
    os.makedirs(BUILD, exist_ok=True)
    exe = os.path.join(BUILD, "compile_script")
    srcs = [os.path.join(SRC, f) for f in sorted(os.listdir(SRC)) if f.endswith(".cpp")]
    newest = max(os.path.getmtime(f) for f in srcs + [os.path.join(HERE, "compile_script.cpp")]
                 + [os.path.join(SRC, f) for f in os.listdir(SRC) if f.endswith(".h")])
    if not os.path.exists(exe) or os.path.getmtime(exe) < newest:
        print("building compile_script ...", flush=True)
        subprocess.run(["c++", "-std=c++17", "-O1", "-w", "-I" + SRC, "-o", exe,
                        os.path.join(HERE, "compile_script.cpp")] + srcs, check=True)
    return exe


def trace_of(text):
    lines = [l.strip() for l in text.splitlines() if l.startswith("EMIT ") or l.strip() == "DONE"]
    return lines


def run_program(script_src, c_src, compiler, gcc, qemu, timeout=20):
    """Returns (status, detail). status: ok | mismatch | crash | rejected | harness."""
    work = tempfile.mkdtemp(prefix="fz_")
    try:
        sc = os.path.join(work, "prog.sc")
        cf = os.path.join(work, "prog.c")
        open(sc, "w").write(script_src)
        open(cf, "w").write(c_src)

        # Reference: host C.
        ref_exe = os.path.join(work, "ref")
        r = subprocess.run(["cc", "-O0", "-fwrapv", "-w", "-o", ref_exe, cf], capture_output=True, text=True)
        if r.returncode != 0:
            return "harness", "C reference did not compile:\n" + r.stderr
        ref = subprocess.run([ref_exe], capture_output=True, text=True, timeout=10).stdout
        expected = trace_of(ref)

        # asmparser2 + QEMU.
        r = subprocess.run([compiler, sc], capture_output=True, text=True, cwd=work)
        if r.returncode != 0:
            return "rejected", (r.stderr + r.stdout)[-2000:]
        header = "\n".join(l for l in r.stdout.splitlines()
                           if l.startswith(("static", "#define", "0x", "};")))
        os.makedirs(os.path.join(work, "inc"))
        open(os.path.join(work, "inc", "script_bytes.h"), "w").write(header + "\n")
        elf = os.path.join(work, "runner.elf")
        r = subprocess.run([gcc, "-mlongcalls", "-specs=sim.elf.specs", "-O0", "-fwrapv", "-w",
                            "-I" + os.path.join(work, "inc"), "-o", elf, os.path.join(HERE, "runner_fuzz.c")],
                           capture_output=True, text=True)
        if r.returncode != 0:
            return "harness", "runner did not link:\n" + r.stderr[-2000:]
        try:
            q = subprocess.run([qemu, "-machine", "esp32", "-nographic", "-kernel", elf],
                               capture_output=True, text=True, timeout=timeout)
            out = q.stdout
        except subprocess.TimeoutExpired as t:
            out = (t.stdout or b"").decode(errors="replace") if isinstance(t.stdout, bytes) else (t.stdout or "")
        got = trace_of(out)
        if "UNSUPPORTED" in out:
            return "harness", out[-500:]
        if not got or got[-1] != "DONE":
            return "crash", "no DONE after %d emits; expected %d\n%s" % (len(got), len(expected), out[-800:])
        if got != expected:
            for n, (a, b) in enumerate(zip(got, expected)):
                if a != b:
                    return "mismatch", "first difference at emit #%d: asmparser2 %s, C %s" % (n, a, b)
            return "mismatch", "asmparser2 emitted %d values, C %d" % (len(got), len(expected))
        return "ok", "%d emits" % len(expected)
    finally:
        shutil.rmtree(work, ignore_errors=True)


# ---------------------------------------------------------------------------
# Reduction
# ---------------------------------------------------------------------------

def statement_slots(prog):
    """Every (list, index) holding a statement, deepest first."""
    slots = []

    def walk(block):
        for i, s in enumerate(block):
            if s["kind"] == "for":
                walk(s["body"])
            elif s["kind"] == "if":
                for _, b in s["branches"]:
                    walk(b)
                if s["else"] is not None:
                    walk(s["else"])
            slots.append((block, i))
    for f in prog["funcs"]:
        walk(f["body"])
    walk(prog["main"])
    return slots


def expression_slots(prog):
    """Every (container, key) holding an expression, so it can be replaced."""
    slots = []

    def walk_expr(container, key):
        e = container[key]
        if not isinstance(e, (list, tuple)) or not e:
            return
        slots.append((container, key))
        kind = e[0]
        if kind in ("bin",):
            walk_expr(e, 2)
            walk_expr(e, 3)
        elif kind == "tern":
            walk_cond(e, 1)
            walk_expr(e, 2)
            walk_expr(e, 3)
        elif kind == "call":
            for i in range(len(e[2])):
                walk_expr(e[2], i)
        elif kind == "idx":
            for i in range(len(e[2])):
                walk_expr(e[2], i)

    def walk_cond(container, key):
        c = container[key]
        if c[0] == "cmp":
            walk_expr(c, 2)
            walk_expr(c, 3)
        else:
            walk_cond(c, 1)
            walk_cond(c, 2)

    def walk_block(block):
        for s in block:
            k = s["kind"]
            if k in ("decl", "emit"):
                walk_expr(s, "expr")
            elif k == "assign":
                walk_expr(s, "expr")
                if s["target"][0] == "idx":
                    for i in range(len(s["target"][2])):
                        walk_expr(s["target"][2], i)
            elif k == "callstmt":
                for i in range(len(s["call"][2])):
                    walk_expr(s["call"][2], i)
            elif k == "for":
                walk_block(s["body"])
            elif k == "if":
                for n in range(len(s["branches"])):
                    walk_cond(s["branches"][n], 0)
                    walk_block(s["branches"][n][1])
                if s["else"] is not None:
                    walk_block(s["else"])
    for f in prog["funcs"]:
        walk_block(f["body"])
        walk_expr(f, "ret")
    walk_block(prog["main"])
    return slots


def to_tuples(x):
    # JSON round-trips turn the tuples into lists; the renderer accepts both,
    # but replacing a node needs mutable containers, so use lists throughout.
    if isinstance(x, (list, tuple)):
        return [to_tuples(v) for v in x]
    if isinstance(x, dict):
        return {k: to_tuples(v) for k, v in x.items()}
    return x


def candidates(prog):
    """Smaller variants of prog: one statement removed, or one expression
    replaced by one of its operands or by a constant."""
    import copy
    out = []
    for n in range(len(statement_slots(prog))):
        trial = copy.deepcopy(prog)
        block, i = statement_slots(trial)[n]
        del block[i]
        out.append(trial)
    for n in range(len(expression_slots(prog))):
        container, key = expression_slots(prog)[n]
        e = container[key]
        options = []
        if e[0] == "bin":
            options += [e[2], e[3]]
        elif e[0] == "tern":
            options += [e[2], e[3]]
        elif e[0] == "call" and e[1] not in ("ix", "dv", "nn"):
            options += list(e[2])
        if e[0] != "lit" or e[1] not in (0, 1):
            options.append(["lit", 1])
        for opt in options:
            trial = copy.deepcopy(prog)
            c2, k2 = expression_slots(trial)[n]
            c2[k2] = copy.deepcopy(opt)
            out.append(trial)
    return out


def size_of(prog):
    return len(statement_slots(prog)) * 10 + len(expression_slots(prog))


def reduce_program(prog, status, compiler, gcc, qemu, log, jobs=8):
    """Shrinks prog while it still fails the same way (same status)."""
    prog = to_tuples(prog)
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        while True:
            trials = sorted(candidates(prog), key=size_of)
            accepted = None
            for start in range(0, len(trials), jobs):
                batch = trials[start:start + jobs]
                results = list(pool.map(
                    lambda t: run_program(render(t, False), render(t, True), compiler, gcc, qemu)[0], batch))
                for t, st in zip(batch, results):
                    if st == status:
                        accepted = t
                        break
                if accepted is not None:
                    break
            if accepted is None:
                return prog
            prog = accepted
            log("  %d statements, %d expressions" % (len(statement_slots(prog)), len(expression_slots(prog))))


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def check_seed(seed, compiler, gcc, qemu):
    prog = Gen(seed).program()
    st, detail = run_program(render(prog, False), render(prog, True), compiler, gcc, qemu)
    return seed, prog, st, detail


def save_failure(seed, prog, status, detail, suffix=""):
    os.makedirs(FAILURES, exist_ok=True)
    base = os.path.join(FAILURES, "seed_%d%s" % (seed, suffix))
    open(base + ".sc", "w").write(render(prog, False))
    open(base + ".c", "w").write(render(prog, True))
    open(base + ".txt", "w").write("%s\n%s\n" % (status, detail))
    import json
    open(base + ".json", "w").write(json.dumps(prog))
    return base


def run_regressions(compiler, gcc, qemu, jobs):
    """Runs regressions/*.json (generated programs, rendered both ways) and
    regressions/*.sc with a matching .c (hand-written pairs, for things the
    generator does not produce, such as global initializers or six-argument
    calls). A hand-written pair's description is its first comment line."""
    import json
    files = os.listdir(REGRESSIONS)
    names = sorted(f[:-5] for f in files if f.endswith(".json"))
    names += sorted(f[:-3] for f in files if f.endswith(".sc") and f[:-3] + ".c" in files
                    and f[:-3] + ".json" not in files)

    def one(name):
        base = os.path.join(REGRESSIONS, name)
        if os.path.exists(base + ".json"):
            prog = json.load(open(base + ".json"))
            script, c, desc = render(prog, False), render(prog, True), prog.get("description", "")
        else:
            script, c = open(base + ".sc").read(), open(base + ".c").read()
            desc = script.splitlines()[0].lstrip("/ ")
        st, detail = run_program(script, c, compiler, gcc, qemu)
        return name, desc, st, detail

    failures = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for name, desc, st, detail in pool.map(one, names):
            if st == "ok":
                print("PASS %s" % name)
            else:
                failures += 1
                print("FAIL %s -- %s: %s\n     (%s)" % (name, st, detail.splitlines()[0], desc))
    print("\n%d of %d regression programs pass" % (len(names) - failures, len(names)))
    return 1 if failures else 0


def add_regression(name, failure, description):
    import json
    base = os.path.splitext(failure)[0]
    prog = json.load(open(base + ".json"))
    prog["description"] = description
    os.makedirs(REGRESSIONS, exist_ok=True)
    json.dump(prog, open(os.path.join(REGRESSIONS, name + ".json"), "w"), indent=1)
    open(os.path.join(REGRESSIONS, name + ".sc"), "w").write(
        "// %s\n// Generated from %s.json by fuzz.py; the .json is the test.\n\n" % (description, name)
        + render(prog, False))
    print("saved regressions/%s.json" % name)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--count", type=int, default=50, help="programs to run (default 50)")
    ap.add_argument("--seed", type=int, help="run exactly this seed")
    ap.add_argument("--start", type=int, default=1, help="first seed when using --count (default 1)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--reduce", metavar="FAILURE", help="reduce a saved failure (its .json or .sc path)")
    ap.add_argument("--print", type=int, metavar="SEED", help="print the script for SEED and exit")
    ap.add_argument("--regressions", action="store_true", help="run the saved regression programs")
    ap.add_argument("--add-regression", nargs=3, metavar=("NAME", "FAILURE", "DESCRIPTION"),
                    help="save a reduced failure as regressions/NAME.json")
    args = ap.parse_args()

    if args.add_regression:
        add_regression(*args.add_regression)
        return 0

    if args.print is not None:
        print(render(Gen(args.print).program(), False))
        return 0

    gcc, qemu = tool_paths()
    compiler = build_compiler()

    if args.regressions:
        return run_regressions(compiler, gcc, qemu, args.jobs)

    if args.reduce:
        import json
        base = os.path.splitext(args.reduce)[0]
        prog = json.load(open(base + ".json"))
        status = open(base + ".txt").readline().strip()
        print("reducing %s (%s, %d statements)" % (base, status, len(statement_slots(prog))))
        small = reduce_program(prog, status, compiler, gcc, qemu, print, args.jobs)
        seed = int(os.path.basename(base).split("_")[1])
        st, detail = run_program(render(small, False), render(small, True), compiler, gcc, qemu)
        out = save_failure(seed, small, st, detail, "_reduced")
        print("reduced reproducer: %s.sc (%s: %s)" % (out, st, detail))
        return 0

    seeds = [args.seed] if args.seed is not None else list(range(args.start, args.start + args.count))
    counts = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(check_seed, s, compiler, gcc, qemu) for s in seeds]
        for fut in concurrent.futures.as_completed(futures):
            seed, prog, st, detail = fut.result()
            counts[st] = counts.get(st, 0) + 1
            if st == "ok":
                continue
            base = save_failure(seed, prog, st, detail)
            print("seed %d: %s -- %s\n   saved %s.sc" % (seed, st.upper(), detail.splitlines()[0], base), flush=True)
    print("\n" + ", ".join("%s: %d" % kv for kv in sorted(counts.items())))
    return 0 if set(counts) <= {"ok"} else 1


if __name__ == "__main__":
    sys.exit(main())
