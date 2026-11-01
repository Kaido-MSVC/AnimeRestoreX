# AnimeRestoreX

Windows x64 desktop tool for AI denoising and 2x/4x upscaling of anime
illustrations, using waifu2x-ncnn-vulkan (models-cunet) and Qt 6.12 Widgets.

Status: v1.0 in development (stage 2: batch, preview, completion dialog).

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
