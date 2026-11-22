# cnc3game.dat — analysis

## Steam build — reference binary ✅
- x86 (PE32), ImageBase 0x400000, entry RVA 0xa2b1, VS2005 (MSVCR80.dll).
- Sections: `.text .rdata .data rts.ver .tls .rsrc` — **no SecuROM, no SteamStub (`.bind`)**.
  `rts.ver` is a tiny (0x200) build/version marker, not protection.
- 17 DLLs, 557 functions. Full list: `imports.txt`.
- Same DLL set as retail; differences: WININET 1 → 9 funcs (HTTP client, likely
  online/news — stub), KERNEL32 +3 (GetFullPathNameA, GetProcessHeap, HeapFree), SHLWAPI +1.
- This is the binary to recompile.

## Retail disc build (old, `imports-retail-securom.txt`): SecuROM
Sections `.securom` (0xB0C000 in memory, only 0xFE000 on disk) and `.memory`,
plus non-standard `rts.ver`, `ars`, `est`, `artem`, `celare`.
The binary unpacks/decrypts itself at runtime, so the on-disk code is not the
code that actually runs. Static recompilation needs an unprotected binary.
Not usable for the port.

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
