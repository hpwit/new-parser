#!/usr/bin/env python3
"""Frame-exact check of the example effect scripts.

Each script is taken from the example sketch that ships it, compiled by
asmparser2 (../fuzz/compile_script.cpp, built from ../../src), run on an
emulated ESP32 under QEMU (runner_effects.c), and compared frame by frame,
byte for byte, with reference.c -- the same algorithm written in C. The
host functions (hsv, rand, sin, millis...) are common.h on both sides.

  ./run.py                 # every effect
  ./run.py fire squares    # some of them

Needs XTENSA_GCC and QEMU as for ../qemu/run.sh, and a host C/C++
compiler. Exit status 0 only if every effect matches.
"""
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(REPO, "src")
BUILD = os.path.join(HERE, "build")

PROCEDURAL = os.path.join(REPO, "examples", "ProceduralEffects", "ProceduralEffects.ino")
MANDELBROT = os.path.join(REPO, "examples", "MandelbrotFixedPoint", "MandelbrotFixedPoint.ino")
MANDELBROT_FFLOAT = os.path.join(REPO, "examples", "MandelbrotFfloat", "MandelbrotFfloat.ino")

# name: (where the script comes from, ms per frame, dump every n frames)
EFFECTS = {
    "plasma": ((PROCEDURAL, "plasmaScript"), 100, 40),
    "fire": ((PROCEDURAL, "fireScript"), 100, 40),
    "voronoi": ((PROCEDURAL, "voronoiScript"), 400, 10),
    "mandala": ((PROCEDURAL, "mandalaScript"), 400, 10),
    "squares": ((PROCEDURAL, "squaresScript"), 400, 10),
    "mandelbrot": ((MANDELBROT, "script"), 100, 40),
    "mandelbrot_ffloat": ((MANDELBROT_FFLOAT, "script"), 100, 40),
    "fixes": (os.path.join(HERE, "scripts", "fixes.sc"), 100, 40),
}


def script_text(source):
    if isinstance(source, str):
        return open(source).read()
    path, name = source
    s = open(path).read()
    m = re.search(r'char %s\[\] = R"EOF\((.*?)\)EOF";' % re.escape(name), s, re.S)
    if not m:
        sys.exit("script %s not found in %s" % (name, path))
    text = m.group(1)
    if name == "script" and path in (MANDELBROT, MANDELBROT_FFLOAT):
        # The sketch redraws forever; one frame is enough to compare.
        text = text.replace("while (true)", "for (int frame = 0; frame < 1; frame++)")
    return text


def build_tools():
    os.makedirs(BUILD, exist_ok=True)
    compiler = os.path.join(BUILD, "compile_script")
    srcs = [os.path.join(SRC, f) for f in sorted(os.listdir(SRC)) if f.endswith(".cpp")]
    inputs = srcs + [os.path.join(SRC, f) for f in os.listdir(SRC) if f.endswith(".h")]
    inputs.append(os.path.join(HERE, "..", "fuzz", "compile_script.cpp"))
    if not os.path.exists(compiler) or os.path.getmtime(compiler) < max(os.path.getmtime(f) for f in inputs):
        print("building compile_script ...", flush=True)
        subprocess.run(["c++", "-std=c++17", "-O1", "-w", "-I" + SRC, "-o", compiler,
                        os.path.join(HERE, "..", "fuzz", "compile_script.cpp")] + srcs, check=True)
    return compiler


def frames(text):
    out, cur = {}, None
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("FRAME "):
            cur = int(line.split()[1])
            out[cur] = []
        elif cur is not None and len(line) == 6 * 128 and re.fullmatch(r"[0-9a-f]+", line):
            out[cur].append(line)
    return out


def check(name, compiler, gcc, qemu):
    source, ms, every = EFFECTS[name]
    work = tempfile.mkdtemp(prefix="fx_")
    try:
        defs = ["-DMS_PER_FRAME=%d" % ms, "-DDUMP_EVERY=%d" % every]
        ref_exe = os.path.join(work, "ref")
        subprocess.run(["cc", "-O0", "-w"] + defs + ["-I" + HERE, "-o", ref_exe,
                        os.path.join(HERE, "reference.c"), "-lm"], check=True)
        expected = frames(subprocess.run([ref_exe, name], capture_output=True, text=True).stdout)

        sc = os.path.join(work, "script.sc")
        open(sc, "w").write(script_text(source))
        r = subprocess.run([compiler, sc], capture_output=True, text=True)
        if r.returncode != 0:
            return name, False, "did not compile: " + (r.stderr + r.stdout)[-300:]
        os.makedirs(os.path.join(work, "inc"))
        open(os.path.join(work, "inc", "script_bytes.h"), "w").write(
            "\n".join(l for l in r.stdout.splitlines() if l.startswith(("static", "#define", "0x", "};"))) + "\n")
        elf = os.path.join(work, "runner.elf")
        r = subprocess.run([gcc, "-mlongcalls", "-specs=sim.elf.specs", "-O0", "-w"] + defs +
                           ["-I" + os.path.join(work, "inc"), "-I" + HERE, "-o", elf,
                            os.path.join(HERE, "runner_effects.c"), "-lm"], capture_output=True, text=True)
        if r.returncode != 0:
            return name, False, "runner did not link: " + r.stderr[-300:]
        try:
            out = subprocess.run([qemu, "-machine", "esp32", "-nographic", "-kernel", elf],
                                 capture_output=True, text=True, timeout=300).stdout
        except subprocess.TimeoutExpired as t:
            out = t.stdout.decode(errors="replace") if isinstance(t.stdout, bytes) else (t.stdout or "")
        if "RESULT DONE" not in out:
            last = [l for l in out.splitlines() if l.startswith("RESULT")]
            return name, False, (last[-1] if last else "no result (crash or hang)")
        got = frames(out)
        if sorted(got) != sorted(expected):
            return name, False, "frames dumped: QEMU %s, C %s" % (sorted(got), sorted(expected))
        bad = [k for k in sorted(expected) if got[k] != expected[k]]
        if bad:
            k = bad[0]
            px = sum(1 for a, b in zip(got[k], expected[k]) for i in range(0, len(a), 6) if a[i:i + 6] != b[i:i + 6])
            return name, False, "frames %s differ (%d pixels in frame %d)" % (bad, px, k)
        return name, True, "%d frames identical" % len(expected)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    gcc, qemu = os.environ.get("XTENSA_GCC"), os.environ.get("QEMU")
    if not gcc or not qemu:
        sys.exit("set XTENSA_GCC and QEMU (see ../qemu/README.md)")
    names = sys.argv[1:] or list(EFFECTS)
    for n in names:
        if n not in EFFECTS:
            sys.exit("unknown effect %s (have: %s)" % (n, ", ".join(EFFECTS)))
    compiler = build_tools()
    ok = True
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for name, good, detail in pool.map(lambda n: check(n, compiler, gcc, qemu), names):
            print("%s %-11s %s" % ("PASS" if good else "FAIL", name, detail), flush=True)
            ok = ok and good
    print("All effects match their C references." if ok else "Some effects do not match.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
