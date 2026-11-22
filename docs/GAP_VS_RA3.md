# Import gap: CC3 (Steam cnc3game.dat) vs RA3-Native runtime

Starting point: RA3-Native covered 431 / 557 imports (77%).

**Now: 557 / 557 registered** (`tools/coverage.py`, also run in CI). The table below is what was added and how deep it goes.

| DLL | Done | Missing | Work |
|---|---|---|---|
| ADVAPI32, IMM32, SHELL32, NETAPI32, WINMM, WS2_32, DSOUND | all | — | none |
| KERNEL32 | 145/157 | GetModuleHandleExA, GetThreadLocale, GetFullPathNameA, QueueUserAPC, CreateProcessA/W, GetExitCodeProcess, Get{Time,Date}Format*, GetEnvironmentStrings, CreateDirectoryA | small, mostly stubs/wrappers |
| MSVCR80 | 147/199 | ctype (`is*`, `isw*`), `_CI*` math intrinsics, 32-bit time (`_time32`, `_localtime32`…), `_stat64i32`, `_wsplitpath`, `_fullpath`, `_mbs*`, a few string funcs | easy, mechanical |
| USER32 | 56/63 | GetDC/ReleaseDC, Get/SetCursorPos, LoadCursorA, EnableMenuItem, wsprintfW | easy |
| ole32 / OLEAUT32 / SHLWAPI | 5/7, 0/2, 2/3 | StringFromGUID2, CoGetObject, SysAllocString/SysFreeString (ord. 2, 6), PathFileExistsA | easy |
| **d3dx9_29** | 0/19 | RA3 uses d3dx9_35: 8 funcs can be aliased (texture loading, FilterTexture, CatmullRom, LoadSurface*). New: matrix/vector math (trivial), D3DXGetFVFVertexSize, D3DXCreateTexture, **D3DXCreateEffect / CreateEffectFromFileA** (map onto existing CreateEffectEx; check CC3 effect binary format) | medium |
| **DINPUT8** | 0/1 | DirectInput8Create + keyboard/mouse device COM objects (RA3 doesn't use DirectInput) | medium — feed from existing touch/input layer |
| **GDI32** | 1/22 | CC3 draws UI text with GDI into DIB sections: CreateFont*, ExtTextOutW, GetGlyphIndicesW, GetCharABCWidthsI, GetTextExtent*, CreateDIBSection, BitBlt… | **largest** — needs a font rasterizer (FreeType or Android Canvas via JNI) |
| WININET | 0/9 | HTTP client for online features | stub (return failure) |

## Order of work
1. Stubs + easy CRT/KERNEL32/USER32 → binary loads and reaches D3D init.
2. d3dx9_29 aliases + math + CreateEffect → first frames.
3. DirectInput8 on top of RA3-Native input.
4. GDI text rendering → menus readable.
5. WININET stays stubbed.

## Done in runtime/cc3_imports.cpp
- Real implementations: KERNEL32, MSVCR80, USER32, ole32, OLEAUT32 (SysAllocString / SysFreeString), SHLWAPI, D3DX math.
- d3dx9_29 → d3dx9_35 alias in `imports.cpp` for texture loading; D3DXCreateTexture, D3DXCreateEffect and
  D3DXCreateEffectFromFileA added in `d3d9.cpp` (FromFileA expects a compiled effect — HLSL source would need a compiler).
- **Stubs**: GDI32 (measures text with a fixed 8x16 cell, draws nothing), DINPUT8 (returns E_FAIL), WININET (no connection).

## Open questions for the first run
- Command line: `"C:\RA3\RetailExe\1.9\cnc3game.dat" -config CNC3_<language>_1.9.SkuDef -win` — check the SkuDef names in the Steam folder.
- Registry: everything reports "not found" (as for RA3). CC3 may want an install path / language key.
- Does the game survive DirectInput8Create failing, or does it need a minimal keyboard/mouse device?
