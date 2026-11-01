# licenses/

Original license and notice texts of the third-party components shipped with
AnimeRestoreX (see `../THIRD_PARTY_NOTICES.md` for what is shipped and why).
Files are unmodified copies; they were collected by `tools/collect-licenses.ps1`.

| Folder | Component | Source of the text |
|---|---|---|
| `Qt/LGPL-3.0-only.txt`, `Qt/GPL-3.0-only.txt` | Qt 6.12.0 | `qtbase-everywhere-src-6.12.0.zip` → `LICENSES/` |
| `Qt/third_party/<Id>/` | Third-party code inside the shipped Qt binaries | Qt 6.12.0 source archives, files named in each `qt_attribution.json`; `index.json` lists them |
| `waifu2x-ncnn-vulkan/` | waifu2x-ncnn-vulkan 20250915 | nihui/waifu2x-ncnn-vulkan @ a86cfb0 |
| `ncnn/` | ncnn (statically linked) | Tencent/ncnn @ c4193aa (tag 20250916) |
| `glslang/` | glslang (statically linked via ncnn) | nihui/glslang @ 8cd77a8 |
| `libwebp/` | libwebp (statically linked) | webmproject/libwebp @ 4fa2191 |
| `dirent/` | dirent (`win32dirent.h`) | tronkko/dirent @ ee812d4 |
| `libjpeg-turbo/`, `libpng/`, `zlib-ng/` | Listed in the upstream build but judged **not linked** into the Windows EXE; included as a precaution | libjpeg-turbo @ 4e151a4, pnggroup/libpng @ 2b97891, zlib-ng @ 4254390 |
| `waifu2x-models/` | Provenance of the CUnet model files | nagadomi/waifu2x @ f2f5c88, lltcggie/waifu2x-caffe @ b272b6f |

The Microsoft Visual C++ runtime files are redistributed under the Visual Studio
"Distributable Code" terms (https://aka.ms/vs/18/redistribution); Microsoft does
not provide a separate license file for them.
