# AnimeRestoreX

Windows x64 desktop tool for AI denoising and 2x/4x upscaling of anime
illustrations, using waifu2x-ncnn-vulkan (models-cunet) and Qt 6.12 Widgets.

Status: v1.0 (stage 4: release package and license materials).

## Release package

```powershell
pwsh -File tools/package.ps1     # release build -> dist/AnimeRestoreX-<ver>-win64.zip
```

Publish together: the zip, the three Qt 6.12.0 source archives copied to `dist/`
(LGPLv3 corresponding source), and `dist/SHA256SUMS.txt`.
License texts: `LICENSE` (MIT, Kaido-MSVC), `THIRD_PARTY_NOTICES.md`, `licenses/`
(regenerate with `tools/collect-licenses.ps1`).

Icons: `resources/icons/app.svg` is the source; the committed `app.ico` and
`app_<size>.png` are regenerated with `pwsh -File tools/make-icons.ps1`
(developer-only, needs Qt Svg). Normal builds use the committed files.

UI languages: 繁體中文, 日本語, English (source). Translations live in
`translations/*.ts`; refresh them with `cmake --build build/debug --target update_translations`.

## Build

Requirements: Qt 6.12.0 msvc2022_64 at `D:\qt\6.12.0\msvc2022_64`,
Visual Studio 2026 with MSVC 14.51, CMake, Ninja (paths in `CMakePresets.json`).

```powershell
pwsh -File build.ps1 -Preset release        # fetches waifu2x on first run
pwsh -File build.ps1 -Preset debug -Test    # with developer tests (uses the GPU)
```

The pinned waifu2x-ncnn-vulkan 20250915 release is fetched and SHA256-checked by
`tools/fetch-waifu2x.ps1`; it is not stored in git.

Specification: `設計規劃.md`, `docs/可行性評估.md`; build/decision log: `docs/開發記錄.md`.
