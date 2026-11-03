# licenses/

Original license and notice texts of the third-party components shipped with
AnimeRestoreX (see `../THIRD_PARTY_NOTICES.md` for what is shipped and why).
Files are unmodified copies; they were collected by `tools/collect-licenses.ps1`.

| Folder | Component | Source of the text |
|---|---|---|
| `Qt/LGPL-3.0-only.txt`, `Qt/GPL-3.0-only.txt` | Qt 6.12.0 | `qtbase-everywhere-src-6.12.0.zip` → `LICENSES/` |
| `Qt/third_party/<Id>/` | Third-party code inside the shipped Qt binaries | Qt 6.12.0 source archives, files named in each `qt_attribution.json`; `index.json` lists them |
| `WindowsML/license.txt` | Microsoft Software License Terms, Microsoft Windows ML Runtime (covers `onnxruntime.dll`, `DirectML.dll`) | NuGet `Microsoft.Windows.AI.MachineLearning` 2.5.96, `license.txt` (SHA-256 `66395f8c…0c376814`) |
| `WindowsML/ThirdPartyNotices.txt` | Third-party notices of the Windows ML Runtime / ONNX Runtime | same package, `ThirdPartyNotices.txt` (SHA-256 `fb0af774…f2f80abf`) |
| `nunif/LICENSE`, `nunif/NOTICE` | nagadomi/nunif, the project the swin_unet/art models come from | nagadomi/nunif @ 31c0137 (the commit the ONNX models were exported with). **The licence of the model weights is not confirmed by upstream yet**; see `THIRD_PARTY_NOTICES.md` §4 |

The Microsoft Visual C++ runtime files are redistributed under the Visual Studio
"Distributable Code" terms (https://aka.ms/vs/18/redistribution); Microsoft does
not provide a separate license file for them.
