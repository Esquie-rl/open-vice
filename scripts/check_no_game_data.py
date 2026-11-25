#!/usr/bin/env python3
"""Fails if the repository tracks game data (CONTRIBUTING.md, "Aucune donnee du jeu").

Checks every tracked file: game and binary extensions, container magics at
the start of a file (DXBC shaders, RPF archives, XVD/MSIXVC packages, PE
executables, Bink video, DDS textures) and size (a source file is small).
Run from the repository root: python3 -I scripts/check_no_game_data.py
"""

import subprocess
import sys

EXTENSIONS = (
    ".rpf", ".ttpf", ".xvd", ".msixvc", ".dxbc", ".dxil", ".fssm", ".spv",
    ".spvasm", ".dds", ".bk2", ".awc", ".ogg", ".wav", ".exe", ".dll",
    ".pdb", ".r9modeldata", ".glb", ".obj",
)
MAGICS = {
    b"DXBC": "DXBC shader container",
    b"RPF": "RPF archive",
    b"msft-xvd": "XVD package",
    b"MZ": "PE executable",
    b"KB2": "Bink video",
    b"DDS ": "DDS texture",
}
# Files the project ships on purpose (tables computed from public specs).
ALLOWED = {"crates/victory-formats/src/bcpack-tables.bin"}
MAX_BYTES = 4 << 20


def main() -> int:
    files = subprocess.run(
        ["git", "ls-files", "-z"], check=True, capture_output=True
    ).stdout.split(b"\0")
    bad = []
    for raw in files:
        if not raw:
            continue
        path = raw.decode("utf-8", "replace")
        if path in ALLOWED:
            continue
        if path.lower().endswith(EXTENSIONS):
            bad.append(f"{path}: game or binary extension")
            continue
        try:
            with open(path, "rb") as f:
                head = f.read(16)
                f.seek(0, 2)
                size = f.tell()
        except OSError:
            continue
        for magic, what in MAGICS.items():
            if head.startswith(magic):
                bad.append(f"{path}: {what}")
                break
        else:
            if size > MAX_BYTES:
                bad.append(f"{path}: {size} bytes (larger than a source file)")
    if bad:
        print("Game data or binaries tracked (CONTRIBUTING.md):")
        print("\n".join(bad))
        return 1
    print(f"No game data among {len([f for f in files if f])} tracked files.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
