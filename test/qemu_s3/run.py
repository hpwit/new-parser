#!/usr/bin/env python3
"""Loops vectorized for the ESP32-S3 (src/vectorize.cpp), run on QEMU's
esp32s3 machine and compared with the same program compiled for the host.

Each program in programs/ (a .sc and its .c reference) is compiled with
the vectorizer on (ASM_VECTORIZE=1), linked into runner_s3.c -- the fuzz
runner with the PIE coprocessor enabled -- and run bare-metal; then
--random N more loops from random_loops.py.

  XTENSA_S3_GCC=.../xtensa-esp32s3-elf-gcc QEMU=.../qemu-system-xtensa ./run.py [--random 100] [--start 1]

QEMU's esp32s3 lacks ee.vldbc without .ip and clamps the saturating adds
one higher than the minimum: neither is used by the vectorizer.
"""
import argparse
import glob
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "fuzz"))
os.environ.setdefault("XTENSA_GCC", "unused")
os.environ.setdefault("QEMU", "unused")
import fuzz  # noqa: E402  (compile_script, trace_of)
import random_loops  # noqa: E402

GCC = os.environ.get("XTENSA_S3_GCC", os.path.expanduser(
    "~/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20240906/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc"))
QEMU = os.environ.get("QEMU") if os.environ.get("QEMU") != "unused" else os.path.expanduser(
    "~/.espressif/tools/qemu-xtensa/esp_develop_9.0.0_20240606/qemu/bin/qemu-system-xtensa")


def run(compiler, sc_src, c_src):
    work = tempfile.mkdtemp(prefix="s3_")
    sc = os.path.join(work, "prog.sc")
    cf = os.path.join(work, "prog.c")
    open(sc, "w").write(sc_src)
    open(cf, "w").write(c_src)
    r = subprocess.run(["cc", "-O0", "-fwrapv", "-w", "-o", os.path.join(work, "ref"), cf], capture_output=True, text=True)
    if r.returncode:
        return "harness", r.stderr
    expected = fuzz.trace_of(subprocess.run([os.path.join(work, "ref")], capture_output=True, text=True).stdout)
    env = dict(os.environ, ASM_VECTORIZE="1")
    r = subprocess.run([compiler, sc], capture_output=True, text=True, cwd=work, env=env)
    if r.returncode:
        return "rejected", (r.stderr + r.stdout)[-1000:]
    os.makedirs(os.path.join(work, "inc"))
    header = "\n".join(l for l in r.stdout.splitlines() if l.startswith(("static", "#define", "0x", "};")))
    open(os.path.join(work, "inc", "script_bytes.h"), "w").write(header + "\n")
    elf = os.path.join(work, "runner.elf")
    r = subprocess.run([GCC, "-mlongcalls", "-specs=sim.elf.specs", "-O0", "-fwrapv", "-w",
                        "-I" + os.path.join(work, "inc"), "-o", elf, os.path.join(HERE, "runner_s3.c")],
                       capture_output=True, text=True)
    if r.returncode:
        return "harness", r.stderr[-1000:]
    log = os.path.join(work, "qemu.log")
    with open(log, "w") as f:
        q = subprocess.Popen([QEMU, "-machine", "esp32s3", "-nographic", "-kernel", elf],
                             stdout=f, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        deadline = time.time() + 30
        while time.time() < deadline:
            time.sleep(0.1)
            if "\nDONE" in open(log, errors="replace").read():
                break
        q.kill()
        q.wait()
    got = fuzz.trace_of(open(log, errors="replace").read())
    if not got or got[-1] != "DONE":
        return "crash", "%d values" % len(got)
    if got != expected:
        for n, (a, b) in enumerate(zip(got, expected)):
            if a != b:
                return "mismatch", "value %d: %s, C %s" % (n, a, b)
        return "mismatch", "%d values, C %d" % (len(got), len(expected))
    return "ok", "%d values" % len(expected)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--random", type=int, default=0)
    ap.add_argument("--start", type=int, default=1)
    args = ap.parse_args()
    compiler = fuzz.build_compiler()
    failed = 0
    for sc in sorted(glob.glob(os.path.join(HERE, "programs", "*.sc"))):
        status, detail = run(compiler, open(sc).read(), open(sc[:-3] + ".c").read())
        print("%-5s %s (%s)" % ("PASS" if status == "ok" else "FAIL", os.path.basename(sc)[:-3], detail))
        failed += status != "ok"
    for seed in range(args.start, args.start + args.random):
        sc, c = random_loops.gen(seed)
        status, detail = run(compiler, sc, c)
        if status != "ok":
            print("FAIL random loop %d: %s %s" % (seed, status, detail))
            failed += 1
    if args.random:
        print("%d random loops" % args.random)
    print("all pass" if failed == 0 else "%d failed" % failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
