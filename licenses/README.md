# licenses/

Original license and notice texts of the third-party components shipped with
AnimeRestoreX (see `../THIRD_PARTY_NOTICES.md` for what is shipped and why).
Files are unmodified copies; they were collected by `tools/collect-licenses.ps1`.

| Folder | Component | Source of the text |
|---|---|---|
| `Qt/LGPL-3.0-only.txt`, `Qt/GPL-3.0-only.txt` | Qt 6.12.0 | `qtbase-everywhere-src-6.12.0.zip` → `LICENSES/` |
| `Qt/third_party/<Id>/` | Third-party code inside the shipped Qt binaries | Qt 6.12.0 source archives, files named in each `qt_attribution.json`; `index.json` lists them |
| `ONNXRuntime/LICENSE` | ONNX Runtime 1.24.4 (MIT, `onnxruntime.dll`) | NuGet `Microsoft.ML.OnnxRuntime.DirectML` 1.24.4, `LICENSE` (hashes of these four files: `tools/collect-licenses.ps1`) |
| `ONNXRuntime/ThirdPartyNotices.txt` | Third-party notices of ONNX Runtime | same package, `ThirdPartyNotices.txt` |
| `DirectML/LICENSE.txt` | Microsoft Software License Terms, DirectML 1.15.4 (`DirectML.dll`) | NuGet `Microsoft.AI.DirectML` 1.15.4, `LICENSE.txt` |
| `DirectML/ThirdPartyNotices.txt` | Third-party notices of DirectML | same package, `ThirdPartyNotices.txt` |
| `nunif/LICENSE`, `nunif/NOTICE` | nagadomi/nunif, the project the waifu2x models come from (MIT; see `THIRD_PARTY_NOTICES.md` §4) | nagadomi/nunif @ 0d3d92c, the commit of the swin_unet_v3 export |

The Microsoft Visual C++ runtime files are redistributed under the Visual Studio
"Distributable Code" terms (https://aka.ms/vs/18/redistribution); Microsoft does
not provide a separate license file for them.
