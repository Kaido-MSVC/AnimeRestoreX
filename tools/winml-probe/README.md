# winml-probe（v2.0 P1，開發用）

不依賴 Qt 的獨立 console 程式，用 Windows ML 套件內附的 ONNX Runtime C/C++ API
對 nunif swin_unet/art 模型跑單一 tile，比較 DirectML 與 CPU 的結果。
不屬於主程式建置，不隨發行包散布。結果與結論見 `docs/v2.0-P0-P1報告.md`。

## 目錄

| 路徑 | 內容 | 版本控制 |
|---|---|---|
| `src/main.cpp` | probe 本體（C++20） | 是 |
| `CMakeLists.txt` | 只用套件內的 `build/cmake` 設定檔，連結 `WindowsML::OnnxRuntime` | 是 |
| `build.ps1` | VS 2026 DevShell、MSVC 14.51、WinGet Ninja 1.13.2，Release 建置 | 是 |
| `fetch-deps.ps1` | 下載固定版本的 nupkg 與模型 zip、驗證 SHA-256、解壓、建立 venv | 是 |
| `run-probe.ps1` | 跑測試矩陣（timing／placement／cancel／compare） | 是 |
| `scripts/` | Python 參考與診斷腳本、`requirements.txt` | 是 |
| `_work/`、`build/` | 下載物、解壓的套件、模型、venv、執行結果 | 否（`.gitignore`） |

## 建置與執行

```powershell
pwsh -File tools\winml-probe\fetch-deps.ps1      # 約 750 MB 下載；已存在且雜湊相符就略過
pwsh -File tools\winml-probe\build.ps1
$py = 'tools\winml-probe\_work\venv\Scripts\python.exe'
& $py -I tools\winml-probe\scripts\reference.py make-input tools\winml-probe\_work\run
& $py -I tools\winml-probe\scripts\reference.py reference tools\winml-probe\_work\run `
      tools\winml-probe\_work\models\onnx_models\swin_unet\art scale2x noise3_scale4x noise3
pwsh -File tools\winml-probe\run-probe.ps1 -Stage timing
pwsh -File tools\winml-probe\run-probe.ps1 -Stage compare
pwsh -File tools\winml-probe\run-probe.ps1 -Stage placement
pwsh -File tools\winml-probe\run-probe.ps1 -Stage cancel -CancelEp dml -CancelDelayMs 50,150
```

DirectML 的負值 Pad 最小重現：

```powershell
& $py -I tools\winml-probe\scripts\make_pad_repro.py tools\winml-probe\_work\run\pad_repro
tools\winml-probe\build\winml_probe.exe --model tools\winml-probe\_work\run\pad_repro\pad_neg_const.onnx `
    --input tools\winml-probe\_work\run\input_256.bin --ep dml --scale 1 --offset 0 --warm 0 --output out.bin
```

## P1 修正：負值 Pad → Slice（`docs/v2.0-P0-P1報告.md` §12）

```powershell
pwsh -File tools\winml-probe\run-p1fix.ps1 -Stage rewrite   # 改寫 14 個模型到 _work\rewritten
& $py -I tools\winml-probe\scripts\test_rewrite_pad.py tools\winml-probe\_work\run\rewrite_selftest
& $py -I tools\winml-probe\scripts\make_manifest.py tools\winml-probe\_work\rewritten models\manifest.json
pwsh -File tools\winml-probe\run-p1fix.ps1 -Stage run       # tile 256：14 個模型 × dml／cpu＋比較
pwsh -File tools\winml-probe\run-p1fix.ps1 -Stage nodes     # 逐節點比對（DML vs CPU）
pwsh -File tools\winml-probe\run-p1fix.ps1 -Stage speed -Only scale2x,noise3_scale4x,noise3 -Tiles 256,400 -Repeat 2
```

`run` 需要先把 `input_256.bin`、`input_core.png` 放到 `_work\p1fix\t256`，並用
`verify_rewrite.py cpu-bitwise` 產生參考值。不要用 640 以上的 tile（6 GB 卡上 4× 模型會 887A0006）。

## P2：tile 拼接、前後處理與 alpha 模組（`docs/v2.0-P2報告.md`）

| 路徑 | 內容 |
|---|---|
| `core/` | 不依賴 Qt 的 C++20 模組：`ModelContract`、`TileProcessor`、`OrtTileEngine`、`Sha256`、`Json`、`Status`（日後可搬進 `src/core/`） |
| `tests/core_tests.cpp` | 用假引擎做的單元測試（不需要 GPU）：`build\core_tests.exe ..\..\models\manifest.json <scratch>` |
| `runner/tile_runner.cpp` | 對單張 ARX8 原始影像執行模組，記錄時間、RAM／VRAM、進度與取消延遲 |
| `scripts/p2_tools.py` | 合成測試圖、PNG ↔ ARX8、與上游比較（含誤差空間分布與熱圖）、alpha 放大圖 |
| `scripts/nunif_weights_check.py` | 確認 nunif .pth 權重與 ONNX 是同一個網路 |
| `run-p2.ps1` | `inputs`／`reference`／`run`／`seams`／`tile160`／`full`／`cancel` 各階段 |

上游參考需要 `_work\upstream\nunif`（commit `0d3d92c`）、`_work\venv-torch`（torch 2.14.1+cpu）以及
`_work\nunif-home`（20250502 的 `swin_unet/art`），建立方式見報告 §5.1。`tile_runner` 讀取的模型目錄是
`_work\models-rw\swin_unet\art\*.onnx`（從 `_work\rewritten` 複製）。

## probe 的行為

- `onnxruntime.dll` 用 `/DELAYLOAD`，`main` 先呼叫 `SetDefaultDllDirectories`，再以 exe 目錄的
  絕對路徑 `LoadLibraryExW` 載入 `DirectML.dll` 與 `onnxruntime.dll`，接著確認模組路徑一致、
  行程內同名模組只有一份，最後才 `Ort::InitApi()`（`ORT_API_MANUAL_INIT`）。
  `GetVersionString()` 不等於 `WINML_EXPECTED_ORT_VERSION`（預設 1.28.3）時以 exit 3 結束。
- SessionOptions：`DisableMemPattern()`、`ORT_SEQUENTIAL`（依 ORT DirectML EP 文件；CPU 路徑也用同樣設定）。
  DML 透過 `GetExecutionProviderApi("DML")` 取得 `OrtDmlApi::SessionOptionsAppendExecutionProvider_DML`
  （標頭把舊的 `OrtSessionOptionsAppendExecutionProvider_DML` 匯出標為 deprecated）。
- 不使用 EP catalog、廠商 EP、`Microsoft.Windows.AI.MachineLearning.dll` 或 experimental 的 WinML Runtime API。
- 輸入：`[1,3,T,T]` float32 RGB 0–1 原始檔；輸出 shape 預期 `[1,3,T*scale-2*offset,…]`。
- 記錄：Session 建立／首次 Run／暖機 Run 時間、Run 間差異、峰值 working set 與 private bytes、
  DXGI `QueryVideoMemoryInfo` 每 10 ms 取樣的本行程 local VRAM 峰值；`--verbose` 擷取節點分配；
  `--profile` 輸出 ORT profiling JSON；`--cancel-delay-ms` 從另一條執行緒呼叫 `SetTerminate()`。

- 診斷用：`--dump-dir D` 跑一次並把每個 float 輸出寫成 `D\<i>.bin`＋`index.txt`；
  `--config key=value` 呼叫 `AddConfigEntry`（例如未公開的 `ep.dml.disable_graph_fusion=1`，正式程式不使用）。

exit code：0 成功；2 ORT 或其他錯誤；3 DLL／版本檢查失敗；4 輸出 shape 不符；64 參數錯誤。
