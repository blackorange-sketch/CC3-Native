#!/usr/bin/env python3
"""List PE header info, sections and imports of a Windows binary.

Pure standard library, no pefile needed. Works on 32-bit (PE32) and 64-bit (PE32+).
Usage: python3 pe_imports.py cnc3game.dat [--json]
"""
import json
import struct
import sys

MACHINES = {0x14C: "x86", 0x8664: "x64"}


def rva_to_off(rva, sections):
    for s in sections:
        if s["va"] <= rva < s["va"] + max(s["vsize"], s["rsize"]):
            return rva - s["va"] + s["raw"]
    return None


def cstr(data, off):
    end = data.index(b"\0", off)
    return data[off:end].decode("latin-1")


def parse(path):
    data = open(path, "rb").read()
    if data[:2] != b"MZ":
        raise SystemExit("not a PE file (no MZ header)")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise SystemExit("not a PE file (no PE signature)")
    machine, nsec, timestamp, _, _, opt_size, chars = struct.unpack_from("<HHIIIHH", data, pe + 4)
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    is64 = magic == 0x20B
    entry = struct.unpack_from("<I", data, opt + 16)[0]
    image_base = struct.unpack_from("<Q" if is64 else "<I", data, opt + (24 if is64 else 28))[0]
    dd_off = opt + (112 if is64 else 96)
    import_rva, import_size = struct.unpack_from("<II", data, dd_off + 8)
    delay_rva = struct.unpack_from("<I", data, dd_off + 13 * 8)[0]

    sections = []
    sec_off = opt + opt_size
    for i in range(nsec):
        name, vsize, va, rsize, raw = struct.unpack_from("<8sIIII", data, sec_off + i * 40)
        sections.append({
            "name": name.rstrip(b"\0").decode("latin-1"),
            "va": va, "vsize": vsize, "rsize": rsize, "raw": raw,
        })

    imports = {}
    off = rva_to_off(import_rva, sections) if import_rva else None
    while off is not None:
        ilt, _, _, name_rva, iat = struct.unpack_from("<IIIII", data, off)
        if not (ilt or name_rva or iat):
            break
        dll = cstr(data, rva_to_off(name_rva, sections))
        funcs = []
        thunk = rva_to_off(ilt or iat, sections)
        size, flag = (8, 1 << 63) if is64 else (4, 1 << 31)
        while thunk is not None:
            val = struct.unpack_from("<Q" if is64 else "<I", data, thunk)[0]
            if val == 0:
                break
            if val & flag:
                funcs.append(f"#ordinal {val & 0xFFFF}")
            else:
                funcs.append(cstr(data, rva_to_off(val & 0x7FFFFFFF, sections) + 2))
            thunk += size
        imports[dll] = funcs
        off += 20

    # delay-load imports (e.g. steam_api.dll): resolved by the game itself through LoadLibrary / GetProcAddress
    delay = {}
    off = rva_to_off(delay_rva, sections) if delay_rva else None
    while off is not None:
        attrs, name_rva, _, _, int_rva = struct.unpack_from("<IIIII", data, off)
        if not name_rva:
            break
        fix = 0 if attrs & 1 else image_base   # old descriptors hold VAs
        dll = cstr(data, rva_to_off(name_rva - fix, sections))
        funcs = []
        thunk = rva_to_off(int_rva - fix, sections) if int_rva else None
        while thunk is not None:
            val = struct.unpack_from("<I", data, thunk)[0]
            if val == 0:
                break
            funcs.append(f"#ordinal {val & 0xFFFF}" if val & (1 << 31) else cstr(data, rva_to_off((val - fix) & 0x7FFFFFFF, sections) + 2))
            thunk += 4
        delay[dll] = funcs
        off += 32

    return {
        "delay_imports": delay,
        "file": path,
        "machine": MACHINES.get(machine, hex(machine)),
        "timestamp": timestamp,
        "image_base": hex(image_base),
        "entry_rva": hex(entry),
        "sections": sections,
        "imports": imports,
    }


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    info = parse(sys.argv[1])
    if "--json" in sys.argv:
        print(json.dumps(info, indent=2))
        return
    print(f"File: {info['file']}\nMachine: {info['machine']}  ImageBase: {info['image_base']}  Entry: {info['entry_rva']}")
    print("\nSections:")
    for s in info["sections"]:
        print(f"  {s['name']:<8} va={s['va']:#010x} vsize={s['vsize']:#x} raw={s['rsize']:#x}")
    # Unusual section names often mean a packer or DRM (SecuROM, SafeDisc, Steam stub)
    known = {".text", ".rdata", ".data", ".rsrc", ".reloc", ".idata", ".tls", ".bss", ".pdata", ".CRT"}
    odd = [s["name"] for s in info["sections"] if s["name"] not in known]
    if odd:
        print(f"  ! non-standard sections (possible protection): {', '.join(odd)}")
    for dll, funcs in info["delay_imports"].items():
        print(f"  delay-loaded: {dll} ({len(funcs)}): {', '.join(funcs[:40])}")
    total = sum(len(f) for f in info["imports"].values())
    print(f"\nImports: {len(info['imports'])} DLLs, {total} functions")
    for dll, funcs in sorted(info["imports"].items(), key=lambda x: x[0].lower()):
        print(f"\n[{dll}] ({len(funcs)})")
        for f in funcs:
            print(f"  {f}")


if __name__ == "__main__":
    main()
