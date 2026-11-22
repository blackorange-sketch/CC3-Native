#!/usr/bin/env python3
"""Import coverage: which imports of the game binary have a host implementation in runtime/.

Usage: python3 tools/coverage.py imports.json runtime/
imports.json comes from tools/pe_imports.py --json. Registrations are found by scanning the runtime sources for
IMP_STD / IMP_C / IMP_ID / registerData calls. DLL aliases mirror runtime/imports.cpp.
"""
import glob
import json
import os
import re
import sys

ALIASES = {"d3dx9_29.dll": "d3dx9_35.dll"}


def registered(root):
    impl = set()
    for f in glob.glob(os.path.join(root, "*.cpp")) + glob.glob(os.path.join(root, "*.h")):
        defs = {}
        for line in open(f, errors="ignore"):
            m = re.match(r'\s*#define\s+(\w+)\s+"([^"]+)"', line)
            if m:
                defs[m.group(1)] = m.group(2)
                continue
            for m in re.finditer(r'(IMP_STD|IMP_C|IMP_ID|registerData|registerFn)\s*\(\s*([^,]+?)\s*,\s*([^,\)]+)', line):
                d, n = m.group(2).strip(), m.group(3).strip().strip('"')
                dll = d.strip('"') if d.startswith('"') else defs.get(d)
                if dll:
                    impl.add((dll.lower(), n))
    return impl


def main():
    imports = json.load(open(sys.argv[1]))["imports"]
    impl = registered(sys.argv[2])
    total = done = 0
    for dll, funcs in sorted(imports.items(), key=lambda x: x[0].lower()):
        d = dll.lower()
        miss = []
        for f in funcs:
            name = f.replace("#ordinal ", "#")
            if (d, name) in impl or (ALIASES.get(d), name) in impl:
                continue
            miss.append(f)
        total += len(funcs)
        done += len(funcs) - len(miss)
        print(f"{dll}: {len(funcs) - len(miss)}/{len(funcs)}" + (f"  missing: {', '.join(miss)}" if miss else ""))
    print(f"\nTOTAL {done}/{total}")


if __name__ == "__main__":
    main()
