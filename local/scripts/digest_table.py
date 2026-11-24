"""digest (the container's HASH part, which a pipeline-library stage blob ends with) -> carved container file, for
the runtime's slot 57 (DeserializeGraphicsPipelineStateX). Writes <spirv>/library_digests.json (local work data).

python -I digest_table.py <work>/shaders/dxbc <work>/shaders/spirv
"""
import collections
import json
import os
import sys

sys.path.insert(0, r"C:\Users\MSI\Desktop\GTA VI\GTA6.Xbox.v1.0.2.1\Leonida-int\tools")
import shader_carve  # noqa: E402


def main():
    dxbc, spirv = sys.argv[1], sys.argv[2]
    with open(os.path.join(dxbc, "index.json"), encoding="utf-8") as f:
        index = json.load(f)
    table, stages = {}, collections.Counter()
    for e in index:
        try:
            with open(os.path.join(dxbc, e["file"]), "rb") as f:
                c = f.read()
            p = shader_carve.parts(c)
        except (OSError, ValueError):
            continue
        if "HASH" not in p or p["HASH"][1] < 20:
            continue
        o = p["HASH"][0]
        d = c[o + 4:o + 20].hex()
        table.setdefault(d, {"file": e["file"], "stage": e["stage"]})
        stages[e["stage"]] += 1
    with open(os.path.join(spirv, "library_digests.json"), "w", encoding="utf-8") as f:
        json.dump(table, f)
    # The same as "<digest> <stage> <file>" lines, for the runtime (file names have no line breaks).
    with open(os.path.join(spirv, "library_digests.txt"), "w", encoding="utf-8", newline="\n") as f:
        for d, v in sorted(table.items()):
            f.write(f"{d} {v['stage']} {v['file']}\n")
    print(len(table), "digests", dict(stages))


if __name__ == "__main__":
    main()
