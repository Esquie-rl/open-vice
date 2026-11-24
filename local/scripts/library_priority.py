"""Translate first the pipeline-library compute programs the game asked for and did not find
("no translated compute shader for FSPX kind-6 and kind-0 sha256 <code sha>" in the runtime's logs).

python -I library_priority.py <work>/shaders <boot folder with *-run-*/stderr.log>
Writes <work>/shaders/library_pri (unpacked + disasm) and <work>/shaders/spirv/library_pri (translations).
"""
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

REPO = r"C:\Users\MSI\Desktop\GTA VI\GTA6.Xbox.v1.0.2.1\Leonida-int"
TOOLS = os.path.join(REPO, "tools")
sys.path.insert(0, TOOLS)
import shader_disasm as disasm_mod  # noqa: E402

MISSING = re.compile(r"no translated compute shader for FSPX kind-6 and kind-0 sha256 ([0-9a-f]{64})")


def main():
    shaders, boot = sys.argv[1], sys.argv[2]
    want = set()
    for log in glob.glob(os.path.join(boot, "*-run-*", "stderr.log")):
        with open(log, encoding="utf-8", errors="replace") as f:
            want.update(MISSING.findall(f.read()))
    print(len(want), "missing compute programs in the logs")
    lib = os.path.join(shaders, "library")
    fspx = os.path.join(lib, "fspx")
    names = []
    for path in glob.glob(os.path.join(fspx, "*.code")):
        with open(path, "rb") as f:
            if hashlib.sha256(f.read()).hexdigest() in want:
                names.append(os.path.basename(path)[:-5])
    print(len(names), "found in the library:", sorted(names))
    out = os.path.join(shaders, "library_pri")
    os.makedirs(os.path.join(out, "fspx"), exist_ok=True)
    os.makedirs(os.path.join(out, "disasm"), exist_ok=True)
    index = []
    jobs = []
    for name in sorted(names):
        for ext in (".code", ".state", ".json"):
            src = os.path.join(fspx, name + ext)
            if os.path.exists(src):
                shutil.copyfile(src, os.path.join(out, "fspx", name + ext))
        if os.path.exists(os.path.join(lib, name + ".dxbc")):
            shutil.copyfile(os.path.join(lib, name + ".dxbc"), os.path.join(out, name + ".dxbc"))
        with open(os.path.join(fspx, name + ".json"), encoding="utf-8") as f:
            meta = json.load(f)
        with open(os.path.join(fspx, name + ".code"), "rb") as f:
            code = f.read()
        entry = {"file": name + ".dxbc", "stage": meta["stage"], "program": meta["program"],
                 "source": meta.get("source", [])}
        for k in ("threads", "wave32"):
            if k in meta:
                entry[k] = meta[k]
        index.append(entry)
        start, end = meta["code"]["start"], meta["code"]["end"]
        if end:
            jobs.append((entry, name, code[start:end], start, end))

    def one(job):
        entry, name, code, start, end = job
        asm, invalid, err = disasm_mod.disasm(code, "gfx1013")
        entry["invalid_words"] = invalid
        with open(os.path.join(out, "disasm", name + ".s"), "w", encoding="utf-8", newline="\n") as f:
            f.write(f"; {name}  code 0x{start:x}..0x{end:x} (fspx), gfx1013, {invalid} invalid words\n")
            f.write(asm)

    with ThreadPoolExecutor(16) as ex:
        list(ex.map(one, jobs))
    with open(os.path.join(out, "index.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(index, f, indent=1)
    spirv = os.path.join(shaders, "spirv", "library_pri")
    os.makedirs(spirv, exist_ok=True)
    r = subprocess.run([sys.executable, "-I", os.path.join(TOOLS, "rdna2spirv.py"), os.path.join(out, "disasm"),
                        "--out", spirv, "--no-asm", "--wave64-emulate"])
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
