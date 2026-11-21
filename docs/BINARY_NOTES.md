# cnc3game.dat — first analysis (patch 1.09, retail)

- x86 (PE32), ImageBase 0x400000, built with VS2005 (MSVCR80.dll).
- 17 DLLs, 545 imported functions. Full list: `imports.txt`.

## Blocker: SecuROM
Sections `.securom` (0xB0C000 in memory, only 0xFE000 on disk) and `.memory`,
plus non-standard `rts.ver`, `ars`, `est`, `artem`, `celare`.
The binary unpacks/decrypts itself at runtime, so the on-disk code is not the
code that actually runs. Static recompilation needs an unprotected binary.
Next step: find a legal DRM-free build (digital release) and re-run the analysis;
a clean binary should show only standard sections.

## Imports worth noting
| DLL | What | Notes |
|---|---|---|
| d3d9 | not imported | loaded at runtime via LoadLibraryA/GetProcAddress |
| d3dx9_29 | 19 funcs | math, texture loading, **D3DXCreateEffect** (effect framework) |
| DINPUT8 | DirectInput8Create | input |
| DSOUND | 5 by ordinal | audio |
| WINMM | timeGetTime etc. | timing |
| WS2_32 | 38 funcs | multiplayer — can be stubbed at first |
| NETAPI32 | Netbios | stub |
| WININET | InternetCrackUrlA | stub |
| KERNEL32 / USER32 / GDI32 / ADVAPI32 | 154 / 63 / 22 / 12 | compare against RA3-Native layer |
| MSVCR80 | 199 | CRT — map to bionic/own shims |
