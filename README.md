# CC3-Native

Native Android port of **Command & Conquer 3: Tiberium Wars** via static recompilation — no Winlator, no Wine, no emulation layer at runtime. Touch controls designed for phones.

Sister project of [RA3-Native](https://github.com/blackorange-sketch/RA3-Native). Both games run on the SAGE engine family, so the recompilation pipeline, Win32/D3D9 compatibility layer and touch scheme are reused from RA3-Native, but CC3 is built and released as a separate game.

> **No game files are included.** You need your own legal copy of C&C 3: Tiberium Wars (patch 1.09 recommended). The launcher copies the required files from your install on first run.

## Status

🚧 Early stage — binary analysis.

| Stage | Status |
|---|---|
| Repo skeleton, CI | ✅ |
| `cnc3game.dat` / `.exe` import analysis | ⏳ |
| Recompilation of the game binary | ⬜ |
| Win32 / D3D9 layer (from RA3-Native) | ⬜ |
| Audio, video (VP6/Bink), input | ⬜ |
| Touch controls | ⬜ |
| First playable build | ⬜ |

## Layout

```
launcher/   Android app (Kotlin): file import, settings, game start
runtime/    Win32 / D3D9 / audio compatibility layer (C/C++), from RA3-Native
recomp/     Static recompiler config and generated code for the CC3 binary
tools/      Analysis scripts (PE imports, game file inventory)
docs/       Roadmap, notes on the CC3 binary and SAGE differences
assets/     Launcher UI assets (no game data)
```

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
python3 tools/pe_imports.py path/to/cnc3game.dat > docs/imports.txt
```

## License

GPL-3.0. Command & Conquer is a trademark of Electronic Arts. This project is not affiliated with or endorsed by EA.
