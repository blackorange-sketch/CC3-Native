# CC3-Native

Native Android port of **Command & Conquer 3: Tiberium Wars** via static recompilation — no Winlator, no Wine, no emulation layer at runtime. Touch controls designed for phones.

Sister project of [RA3-Native](https://github.com/blackorange-sketch/RA3-Native). Both games run on the SAGE engine family, so the recompilation pipeline, Win32/D3D9 compatibility layer and touch scheme are reused from RA3-Native, but CC3 is built and released as a separate game.

> **No game files are included.** You need your own legal copy of C&C 3: Tiberium Wars — the **Steam** release (the retail disc build is SecuROM-protected and cannot be recompiled).

## Status

🚧 Bring-up. Runtime, recompiler and launcher come from RA3-Native; CC3 specifics live in `runtime/game_build.h` and `runtime/cc3_imports.cpp`.

| Stage | Status |
|---|---|
| Binary analysis (Steam `cnc3game.dat`, no SecuROM) | ✅ |
| Runtime, recompiler, launcher, CI from RA3-Native | ✅ |
| All 557 imports registered (`tools/coverage.py`) | ✅ |
| First CI build of the APK | ⏳ |
| Game reaches D3D init / first frames | ⬜ |
| DirectInput8 (stub now) | ⬜ |
| GDI text rendering (stub now: no menu text) | ⬜ |
| Touch controls tuned for CC3 | ⬜ |
| First playable build | ⬜ |

## Layout

```
android/    Android app: launcher (Java) and native glue (android_main.cpp)
runtime/    Win32 / D3D9 / audio layer the recompiled game runs on
tools/      analyze + lift_pe (static recompiler), pe_imports.py, coverage.py
tests/      lifter tests (from RA3-Native)
docs/       binary notes, import gap, roadmap
```

## Game folder

Point the launcher at the CC3 folder (it finds `RetailExe/1.9/cnc3game.dat` by itself). The language list comes from
the `CNC3_<language>_1.9.SkuDef` files in the game root.

## Touch controls (planned, same as RA3-Native)

- One finger drag — pan the map
- Hold, then drag — selection box
- Two fingers — pan / pinch zoom
- Long press (0.6 s) — right click
- Two-finger tap — cancel

## Building

CI builds on GitHub Actions. The game binary is never stored in this repo: CI pulls it from a private repo through the `GAME_REPO_TOKEN` secret (same setup as RA3-Native).

## Analysing your binary locally

```
python3 tools/pe_imports.py path/to/cnc3game.dat --json > imports.json
python3 tools/coverage.py imports.json runtime
```

## License

GPL-3.0. Command & Conquer is a trademark of Electronic Arts. This project is not affiliated with or endorsed by EA.
