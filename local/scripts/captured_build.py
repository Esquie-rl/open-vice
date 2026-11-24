"""Translate the shaders the runtime captured (LEONIDA_CAPTURE_SHADERS) with the corpus's own chain:
carve-style index.json -> fssm_unpack.py -> shader_disasm.py -> rdna2spirv.py (+ --merge), into <spirv>/captured.

python -I captured_build.py <work>/shaders/captured <work>/shaders/spirv/captured
"""
import json
import os
import subprocess
import sys

REPO = r"C:\Users\MSI\Desktop\GTA VI\GTA6.Xbox.v1.0.2.1\Leonida-int"
TOOLS = os.path.join(REPO, "tools")
sys.path.insert(0, TOOLS)
import shader_carve  # noqa: E402


def run(*args):
    print(">", " ".join(os.path.basename(a) if i == 0 else a for i, a in enumerate(args)), flush=True)
    r = subprocess.run([sys.executable, "-I", *args])
    if r.returncode:
        print(f"  exit {r.returncode}", flush=True)
    return r.returncode


def main():
    src, out = sys.argv[1], sys.argv[2]
    index = []
    for name in sorted(os.listdir(src)):
        if not name.endswith(".dxbc"):
            continue
        with open(os.path.join(src, name), "rb") as f:
            data = f.read()
        try:
            p = shader_carve.parts(data)
            stage, sm = shader_carve.shader_model(data, p)
        except ValueError as e:
            print(f"skip {name}: {e}")
            continue
        index.append({"name": name[:-5], "file": name, "offset": 0, "kind": 0, "stage": stage, "sm": sm,
                      "size": len(data), "parts": {k: v[1] for k, v in p.items()}})
    print(f"{len(index)} captured containers:", {e['name']: e['stage'] for e in index})
    with open(os.path.join(src, "index.json"), "w", encoding="utf-8") as f:
        json.dump(index, f, indent=1)
    if not index:
        return 1
    if run(os.path.join(TOOLS, "fssm_unpack.py"), src, "--carve-index", os.path.join(src, "index.json")):
        return 1
    if run(os.path.join(TOOLS, "shader_disasm.py"), os.path.join(src, "fssm"), "--unpack-index",
           os.path.join(src, "fssm_index.json"), "--out", os.path.join(src, "disasm"), "--mcpu", "gfx1013"):
        return 1
    os.makedirs(out, exist_ok=True)
    rc = run(os.path.join(TOOLS, "rdna2spirv.py"), os.path.join(src, "disasm"), "--out", out, "--no-asm",
             "--wave64-emulate")
    return rc


if __name__ == "__main__":
    sys.exit(main())
