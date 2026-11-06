# AnimeRestoreX

Windows x64 desktop tool for AI denoising (1x) and 2x/4x upscaling of anime
illustrations, using the waifu2x swin_unet_v3 and swin_unet models (nagadomi/nunif,
ONNX) on ONNX Runtime + DirectML and Qt 6.12 Widgets.

## Features

- AI denoising without upscaling (1x) and 2x upscaling with waifu2x swin_unet_v3/art,
  4x upscaling with swin_unet/art; PNG, JPEG and WebP input and output (WebP lossy or lossless).
- GPU through DirectML (any DirectX 12 GPU), or the CPU when chosen in the settings.
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
take `AnimeRestoreX-<version>-win64-with-models.zip`, extract the entire archive,
then run `AnimeRestoreX.exe` (no installation; the AI models are included). To check
the download, compare its SHA256 with `SHA256SUMS-<package name>.txt` attached to the
same release. The executable is unsigned.

v1.2 and older stay on the Releases page; they use waifu2x-ncnn-vulkan and need
Windows 10 1809+ or Windows 11 (x64), a Vulkan-capable GPU and a compatible driver.

## System requirements (v2.0)

| | Requirement | Basis |
|---|---|---|
| OS | **Windows 11 x64** (tested: 25H2, build 26200) | the only tested system |
| | Windows 10: **not tested** | Qt 6.12: Windows 10 1809+ / 11 (Qt 6.12 is the last Qt with Windows 10 support); the minimum of the upstream runtime was not checked |
| GPU | DirectX 12 GPU with a current driver. **Tested: NVIDIA GeForce GTX 1060 6GB** only | DirectML 1.15.4 |
| GPU memory | Auto tile 256: about 1.2 GB at 1x/2x, 0.8 GB at 4x per model (twice that with transparency); tile 448 about 3 GB | measured on the GTX 1060 (P1 redo) |
| RAM | **8 GB recommended**. Peak private memory of the app: about 1.3 GB at 1x, 1.7 GB at 2x, 1.2 GB at 4x, 2.1 GB at 4x with transparency | measured (1x/2x P1 close, 4x P3) |
| CPU mode | works without a GPU, about 3 to 10 times slower than the GTX 1060 on the test PC, depending on the scale | measured (P2 redo) |

Not tested: AMD and Intel GPUs, GPUs with less than 6 GB, more than one GPU (the
Windows default adapter is always used), Windows 10, ARM64, remote desktop and
virtual machines.

## Known limitations (v2.0)

- **Cancel during encoding waits for the encoder.** A cancel during AI processing
  stops within about 0.1 s (GPU) or immediately (CPU), but a file that is being
  encoded is finished first and then discarded: up to about 6 s for a 4x WebP
  lossless output. The status bar says "finishing the encoding of the current file".
  No output file is left behind.
- **The tile size changes the result slightly** (a property of the swin_unet
  models, the same upstream). Auto uses 256 and falls back to 160 only when GPU
  memory is short or runs out; such items are marked in the list and in the
  summary. Choose a fixed tile size in the settings for identical results.
- The first image of a batch takes a few seconds longer (the AI models are loaded
  and checked); the time estimate learns this separately.
- After the GPU has been used, about 280 MB of memory stays allocated by the
  DirectML runtime until the program closes (it does not grow with more images).
- v1.2 and v2.0 share the language, folder and window settings; the AI settings
  and measured speeds of v2.0 are kept separately ("v2/"), so v1.2 keeps its own.

## Development notes

UI plan: `docs/v1.1-ui-plan.md`; build and decision log: `docs/開發記錄.md`.
Theme: `src/ui/Theme.cpp` (tokens + style sheet). The chevron images in
`resources/theme/` are regenerated with `pwsh -File tools/make-theme-arrows.ps1`.

## Release package

```powershell
pwsh -File tools/package.ps1                  # dist/AnimeRestoreX-<ver>-win64.zip (no models)
pwsh -File tools/package.ps1 -IncludeModels   # dist/AnimeRestoreX-<ver>-win64-with-models.zip (the 14 models)
```

`-Suffix <tag>` adds a test-build tag to the package name and to `MANIFEST.txt`.

The script configures and builds a clean Release in `.package/build` (tests off),
assembles a fresh staging folder from an explicit allowlist (nothing is taken from
`build/` or `dist/`), and refuses to zip if any file is not on the allowlist, an
old-engine file appears, a copy differs from its source, `onnxruntime.dll` /
`DirectML.dll` do not match the version and SHA-256 in `models/manifest.json`, or a
shipped binary imports a DLL that is neither shipped nor part of Windows. The zip is
re-extracted and compared file by file. While `models/manifest.json` says the model
licence is `UNRESOLVED`, a package with models is written to
`tools/winml-probe/_work/p5/` with `-INTERNAL` in its name, never to `dist/`.

Publish together: the zip, the three Qt 6.12.0 source archives copied next to it
(LGPLv3 corresponding source), and `SHA256SUMS-<package name>.txt`.
License texts: `LICENSE` (MIT, Kaido-MSVC), `THIRD_PARTY_NOTICES.md.in`, `licenses/`
(regenerate with `tools/collect-licenses.ps1`). The build writes
`THIRD_PARTY_NOTICES.md` next to the executable and copies `LICENSE` and `licenses/`
there; the package ships them. There is no first-run agreement: Help > About >
Licenses and Notices shows every licence and third-party notice.

Icons: `resources/icons/app.svg` is the source; the committed `app.ico` and
`app_<size>.png` are regenerated with `pwsh -File tools/make-icons.ps1`
(developer-only, needs Qt Svg). Normal builds use the committed files.

UI languages: 繁體中文, 日本語, English (source). Translations live in
`translations/*.ts`; refresh them with `cmake --build build/debug --target update_translations`.

## Build

Requirements: Qt 6.12.0 msvc2022_64 at `D:\qt\6.12.0\msvc2022_64`,
Visual Studio 2026 with MSVC 14.51, CMake, Ninja (paths in `CMakePresets.json`).

```powershell
pwsh -File build.ps1 -Preset release        # fetches the runtime and models on first run
pwsh -File build.ps1 -Preset debug -Test    # with developer tests (some use the GPU)
```

`tst_ui_layout` runs the window size / layout checks on the real `windows`
platform (windows flash briefly); the other UI tests run offscreen.

The pinned ONNX Runtime 1.24.4 and DirectML 1.15.4 packages are fetched and
SHA-256-checked by `tools/fetch-runtime.ps1` (`build.ps1 -OrtFlavor windowsml` builds
against the Windows ML 2.5.96 package instead; `tools/package.ps1` does not package
it); the nunif models are fetched, converted and checked against `models/manifest.json` by
`tools/fetch-models.ps1`. Neither is stored in git.

Specification: `設計規劃.md`, `docs/可行性評估.md`; build/decision log: `docs/開發記錄.md`.
