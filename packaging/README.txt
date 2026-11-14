AnimeRestoreX 2.0
=================

動漫插畫 AI 降噪與 1×（只降噪）/ 2× / 4× 放大工具
（waifu2x swin_unet_v3 / swin_unet 模型，ONNX Runtime + DirectML）。
AI denoising and 1x (denoise only) / 2x / 4x upscaling for anime illustrations
(waifu2x swin_unet_v3 / swin_unet models on ONNX Runtime + DirectML).


系統需求 / Requirements
-----------------------
- Windows 11 x64（實測：25H2 build 26200）。Windows 10 未測試。
- DirectX 12 顯示卡與最新驅動程式；已實測的只有 NVIDIA GeForce GTX 1060 6GB。
  沒有合適 GPU 時可在「設定」選擇 CPU（在測試電腦上約慢 3 到 10 倍，視倍率而定）。
- 建議 8 GB 記憶體（4× 有透明時程式約用 2.1 GB）。開啟 TTA 時建議 16 GB：
  最大尺寸的 4× 透明圖約用 4.4 GB。
- 不需要安裝；不需要另外安裝 Visual C++ 執行階段；不需要網路連線。
- 未測試：AMD / Intel GPU、6 GB 以下的 GPU、多 GPU（固定使用 Windows 預設的
  顯示卡）、Windows 10、ARM64、遠端桌面與虛擬機器。
- 程式未經數位簽章；第一次執行時 Windows SmartScreen 可能會顯示警告。


AI 模型 / AI models
-------------------
本程式使用 nagadomi/nunif 的 waifu2x 模型（MIT 授權）：1×、2× 是 swin_unet_v3/art，
4× 是 swin_unet/art，共 14 個 .onnx 檔，放在 models\ 底下。
**未附模型的版本**（models 資料夾裡只有 manifest.json）無法處理圖片，
處理時會顯示「缺少 AI 模型檔」。取得含模型的版本後，把它的 models 資料夾
整個複製到 AnimeRestoreX.exe 旁邊即可；程式會用 models\manifest.json 中的
SHA256 檢查每個模型檔，不符就拒絕使用。

The models are MIT licensed. A package without models (only models\manifest.json)
cannot process images and says that the AI model files are missing. Copy the
complete "models" folder of a package that includes them next to AnimeRestoreX.exe;
every file is checked against the SHA256 in models\manifest.json.


使用方式
--------
1. 將整個資料夾解壓縮到任意位置後，執行 AnimeRestoreX.exe。
2. 「匯入圖片…」或「匯入資料夾…」加入 PNG / JPEG / WebP 圖片，
   也可以直接把圖片或資料夾從檔案總管拖曳到清單
   （資料夾只匯入該層，不含子資料夾）。
3. 「處理」選擇放大倍率（不放大 1× / 2× / 4×）與降噪強度；
   「輸出」選擇資料夾、格式與品質（WebP 可選有損 / 無損），按「開始處理」。
   「不放大（1×）」只做降噪，必須選擇降噪強度。
4. 完成後會依「失敗 / 未處理 / 成功」列出每個項目，可直接開啟輸出資料夾。

- 「工具 → 設定」：Tile 大小（自動 / 160 / 256 / 352 / 448）、處理裝置（GPU / CPU）、
  介面語言（繁體中文 / 日本語 / English）。
- 快捷鍵：Ctrl+O 匯入圖片、Ctrl+Shift+O 匯入資料夾、Ctrl+Enter 開始處理、
  Delete 移除選取項目、Ctrl+, 設定。
- 不會修改或覆寫原始圖片；同名輸出會自動加上 (1)、(2)…。
- 輸入上限 4.5 MP（例如 3000 x 1500），輸出上限 75 MP；WebP 單邊上限 16383 px。
- 不支援 16 位元、HDR、動畫圖片。


已知限制 / Known limitations
----------------------------
- 編碼中取消要等目前檔案編碼完成（4× WebP 無損最多約 6 秒），不會留下輸出檔。
- Tile 大小會讓結果略有不同（模型本身的特性）。「自動」用 256，GPU 記憶體
  不足時改用 160，這些項目會在清單與完成摘要中標示。
- 每批第一張會慢幾秒（載入並檢查模型）。
- 用過 GPU 之後約有 280 MB 記憶體保留到程式關閉（不會隨張數增加）。

- Cancelling during encoding waits until the current file is encoded (up to about
  6 s for 4x WebP lossless); no output file is left behind.
- The tile size changes the result slightly. Auto uses 256 and 160 when GPU memory
  is short; such items are marked in the list and the summary.
- The first image of a batch takes a few seconds longer.


Usage (English)
---------------
Extract the whole folder anywhere and run AnimeRestoreX.exe. Add images or a
folder (or drag them onto the list), choose upscale (1x = denoise only, 2x, 4x)
and denoise level, the output folder, format and quality, then press Start
(Ctrl+Enter). Tools > Settings: tile size, processing device (GPU / CPU),
language. Source images are never modified; existing output files are never
overwritten.


移除 / Uninstall
----------------
刪除整個資料夾即可。設定存於 HKEY_CURRENT_USER\Software\Kaido-MSVC\AnimeRestoreX
（語言、資料夾、視窗大小與 AnimeRestoreX 1.x 共用；2.0 專屬的設定在其中的 v2
機碼）。如果不再使用任何版本，可刪除整個機碼。

Delete the folder. Settings are kept in HKEY_CURRENT_USER\Software\Kaido-MSVC\
AnimeRestoreX (shared with AnimeRestoreX 1.x; the 2.0-only settings are under "v2").


授權 / License
--------------
- 所有授權與第三方聲明可在「說明 → 關於 AnimeRestoreX → 授權與聲明」查看。
  轉送給他人時，請保持整個資料夾完整、不做修改（含 LICENSE、
  THIRD_PARTY_NOTICES.md 與 licenses 資料夾）；不得單獨散布 DirectML.dll。
- AnimeRestoreX: MIT License, Copyright (c) 2026 Kaido-MSVC（LICENSE）
- 第三方元件（Qt 6.12.0 LGPLv3、ONNX Runtime MIT、DirectML、Visual C++ 執行階段、
  模型等）各自適用原授權，詳見 THIRD_PARTY_NOTICES.md 與 licenses\ 資料夾。
- Qt 6.12.0 對應原始碼（qtbase / qtimageformats / qttranslations，未修改）
  與本 zip 一同發布於同一位置；檔名與 SHA256 見 THIRD_PARTY_NOTICES.md §2。
- 各檔案的 SHA256 見 MANIFEST.txt。
