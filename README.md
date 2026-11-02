# AnimeRestoreX

Windows x64 desktop tool for AI denoising (1x) and 2x/4x upscaling of anime
illustrations, using waifu2x-ncnn-vulkan (models-cunet) and Qt 6.12 Widgets.

## Features

- AI denoising without upscaling (1x) and 2x / 4x upscaling with the waifu2x
  CUnet model; PNG, JPEG and WebP input and output (WebP lossy or lossless).
- Batch list with thumbnails; add images or whole folders, also by drag and drop.
- Side-by-side preview of the original and the result (fit to window or 100%).
- Progress for the current image (reading → AI processing → encoding) and for
  the whole batch, with an estimated time left. The estimate is based on the
  image size and the speed measured on your GPU, and is marked as an estimate.
- Light and dark theme that follows the Windows setting.
- User interface in 繁體中文, 日本語 and English; high-DPI aware.

The changes in each version are listed on the [Releases page](https://github.com/Kaido-MSVC/AnimeRestoreX/releases).

## Download

Download the latest version from the
[Releases page](https://github.com/Kaido-MSVC/AnimeRestoreX/releases/latest):
take `AnimeRestoreX-<version>-win64.zip`, extract the entire archive, then run
`AnimeRestoreX.exe` (no installation). To check the download, compare its
SHA256 with `SHA256SUMS.txt` attached to the same release.

Requires Windows 10 1809+ or Windows 11 (x64), a Vulkan-capable GPU and a
compatible driver. The executable is unsigned.

## Development notes

UI plan: `docs/v1.1-ui-plan.md`; build and decision log: `docs/開發記錄.md`.
Theme: `src/ui/Theme.cpp` (tokens + style sheet). The chevron images in
`resources/theme/` are regenerated with `pwsh -File tools/make-theme-arrows.ps1`.

## Release package

```powershell
pwsh -File tools/package.ps1     # release build -> dist/AnimeRestoreX-<ver>-win64.zip
```

Publish together: the zip, the three Qt 6.12.0 source archives copied to `dist/`
(LGPLv3 corresponding source), and `dist/SHA256SUMS.txt`.
License texts: `LICENSE` (MIT, Kaido-MSVC), `THIRD_PARTY_NOTICES.md.in`, `licenses/`
(regenerate with `tools/collect-licenses.ps1`). The build writes
`THIRD_PARTY_NOTICES.md` next to the executable with `@PROJECT_VERSION@`
replaced by the version in `CMakeLists.txt`; the package ships that file.

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

`tst_ui_layout` runs the window size / layout checks on the real `windows`
platform (windows flash briefly); the other UI tests run offscreen.

The pinned waifu2x-ncnn-vulkan 20250915 release is fetched and SHA256-checked by
`tools/fetch-waifu2x.ps1`; it is not stored in git.

Specification: `設計規劃.md`, `docs/可行性評估.md`; build/decision log: `docs/開發記錄.md`.
