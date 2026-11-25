<p align="center"><img src="docs/icon.png" width="128" alt="CC3 Native icon"></p>

# CC3 Native

**Command & Conquer 3: Tiberium Wars running natively on Android.** The game's own code is translated ahead of time
from x86 to ARM64 (static recompilation) and runs on a small Win32 / Direct3D 9 layer built on OpenGL ES — no Wine,
no Winlator, no emulator at run time. Controls are made for a phone touch screen.

> **No game files are included.** You need your own copy of C&C 3: Tiberium Wars from **Steam**
> (e.g. *C&C: The Ultimate Collection*). The old retail disc build is SecuROM-protected and cannot be recompiled.

## Status

🚧 **Early bring-up — not playable yet.**

| Stage | |
|---|---|
| Binary analysis — Steam `cnc3game.dat` has no SecuROM | ✅ |
| Recompiler, runtime and launcher in place, APK builds in CI | ✅ |
| All 557 imports of the game have a host implementation | ✅ |
| Game reaches D3D init / first frames | ⏳ |
| DirectInput8 keyboard / mouse | ⬜ stub |
| UI text (GDI fonts) | ⬜ stub — menus have no text yet |
| Touch controls tuned for CC3 | ⬜ |
| Playable skirmish | ⬜ |

Details: [`docs/BINARY_NOTES.md`](docs/BINARY_NOTES.md), [`docs/GAP_VS_RA3.md`](docs/GAP_VS_RA3.md), [`docs/ROADMAP.md`](docs/ROADMAP.md).

## Installing

1. Install the game with Steam on a PC and copy the whole **Command and Conquer 3 Tiberium Wars** folder to the phone
   (any place in internal storage).
2. Install `cc3-native-*.apk` from [Releases](../../releases) (`nightly` = latest test build).
3. Allow *All files access* when asked, then press **GAME FOLDER** if the game was not found by itself.
4. The status line turns **READY** when the game file matches the build — then **PLAY**.

The Steam folder can contain several versions (`RetailExe/1.9`, `RetailExe/1.10`). The launcher picks the
`cnc3game.dat` the APK was recompiled from and the matching `CNC3_<language>_<version>.SkuDef`. The language list
shows what is installed in the folder — switch the game language in Steam before copying.

Requirements: Android 10+, 64-bit ARM, OpenGL ES 3.

## Controls

| Gesture | Action |
|---|---|
| Tap | left click |
| Long press (0.6 s) | right click |
| Hold, then drag | selection box |
| One-finger swipe | move the camera |
| Two fingers | pan / pinch zoom |
| Two-finger tap | cancel |

On wide screens the side columns hold hotkey buttons (SHIFT / CTRL / ALT and more).

## Logs and debug flags

With **LOG FILE** on, the runtime writes `/sdcard/cc3_native.log` — attach it to bug reports. Empty files in
`/sdcard` switch on diagnostics, e.g. `cc3_verbose` (more logging), `cc3_play_movies`, `cc3_fxsafe` (no effect
shortcuts), `cc3_nobatch` (no draw merging).

## How it works

```
cnc3game.dat ──analyze──► function list ──lift_pe──► C++ per function ──NDK──► libcc3native.so
                                                                            + runtime (Win32, D3D9 → GLES, audio)
```

- `tools/` — `analyze` finds the functions, `lift_pe` turns each into C++, `pe_imports.py` / `coverage.py` check which
  imports the runtime provides.
- `runtime/` — loader, CPU state, KERNEL32 / USER32 / CRT / WinSock, DirectSound → AAudio, D3D9 + D3DX effects →
  OpenGL ES. Game-specific settings: `game_build.h`; CC3-only imports: `cc3_imports.cpp`.
- `android/` — the launcher (Java) and the native glue (`android_main.cpp`: window, input, HUD buttons).
- `tests/` — differential tests of the lifter against the real x86 CPU.

## Building

Everything builds on GitHub Actions (`.github/workflows/android.yml`). The game binary and the generated code are never
committed or published: CI downloads `cnc3game.dat` from a **private** repository and uses it only inside the job.

To build your own fork:

1. Create a private repo `<you>/cc3-private` with a release tagged `game-binary` containing your `cnc3game.dat`.
2. Create a fine-grained token with *Contents: read* on that repo, add it to the fork as the `GAME_REPO_TOKEN`
   Actions secret, and change the repo name in the workflow's *Fetch cnc3game.dat* step.
3. Run **Actions → Android APK**. The APK appears as an artifact and in the `nightly` release.

Checking a binary locally:

```
python3 tools/pe_imports.py path/to/cnc3game.dat            # sections (SecuROM shows up here) and imports
python3 tools/pe_imports.py path/to/cnc3game.dat --json > imports.json
python3 tools/coverage.py imports.json runtime                # which imports the runtime implements
```

## Credits

The recompiler and runtime started from [RA3-Native](https://github.com/blackorange-sketch/RA3-Native), the author's
Red Alert 3 port on the same engine family (SAGE).

## License

GPL-3.0 — see [LICENSE](LICENSE).

Command & Conquer is a trademark of Electronic Arts Inc. This is an unofficial fan project, not affiliated with or
endorsed by EA. It contains no game code or assets; you need your own copy of the game.
