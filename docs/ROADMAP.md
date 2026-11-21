# Roadmap

## 0. Binary analysis
- Pick the reference build: C&C 3 Tiberium Wars **1.09** (decide: with or without Kane's Wrath).
- Note: in CC3 the real game code lives in `RetailExe/1.9/cnc3game.dat`; `CNC3.exe` is only a launcher.
- Run `tools/pe_imports.py` on `cnc3game.dat`, commit the result to `docs/imports.txt`.
- Check for protection (SecuROM on disc versions, Steam/EA App wrappers). Non-standard PE sections are a hint.
- Diff the import list against RA3 1.12 — whatever RA3-Native already implements is free.

## 1. Bring over from RA3-Native
- Recompiler pipeline and CI (private game repo + `GAME_REPO_TOKEN`).
- Win32 / D3D9 compatibility layer.
- Launcher: first-run file import, settings, log toggle (off by default in stable).
- Touch controls.

## 2. CC3-specific work
- Missing Win32 / D3D9 calls found in step 0.
- Video: CC3 uses VP6 (`.vp6`) cutscenes — needs a decoder.
- Audio: check which middleware the binary loads.
- Game files: `.big` archives and the asset/manifest format of this SAGE version.
- Shaders: compare with the EA modding support pack.

## 3. First playable build
- Skirmish map loads and runs.
- Then: campaign, cutscenes, UI scaling for phone screens, widescreen.

## Releases
- Nightly builds for testing, stable builds on the Releases page (same as RA3-Native).
- Minimum Android 10.
