AnimeRestoreX 1.1.1
====================

動漫插畫 AI 降噪與 1×（只降噪）/ 2× / 4× 放大工具（waifu2x CUnet，Vulkan GPU）。
AI denoising and 1x (denoise only) / 2x / 4x upscaling for anime illustrations (waifu2x CUnet, Vulkan GPU).


系統需求 / Requirements
-----------------------
- Windows 10 1809 以上（x64）或 Windows 11
- 支援 Vulkan 的顯示卡與最新驅動程式（NVIDIA / AMD / Intel）
- 不需要安裝；不需要另外安裝 Visual C++ 執行階段


使用方式
--------
1. 將整個資料夾解壓縮到任意位置後，執行 AnimeRestoreX.exe。
   資料夾路徑請勿過長（AI 引擎的模型路徑上限約 255 個字元）。
2. 「匯入圖片…」或「匯入資料夾…」加入 PNG / JPEG / WebP 圖片，
   也可以直接把圖片或資料夾從檔案總管拖曳到清單
   （資料夾只匯入該層，不含子資料夾）。
3. 「處理」選擇放大倍率（不放大 1× / 2× / 4×）與降噪強度；
   「輸出」選擇資料夾、格式與品質（WebP 可選有損 / 無損），按「開始處理」。
   「不放大（1×）」只做降噪，必須選擇降噪強度。
4. 完成後會顯示成功 / 失敗數，可直接開啟輸出資料夾。

- 「工具 → 設定」可調整 tile 大小與介面語言（繁體中文 / 日本語 / English）。
- 快捷鍵：Ctrl+O 匯入圖片、Ctrl+Shift+O 匯入資料夾、Ctrl+Enter 開始處理、
  Delete 移除選取項目、Ctrl+, 設定。
- 視窗大小與位置會記住；高 DPI（125% / 150%）下會自動調整到螢幕可用範圍內。
- 不會修改或覆寫原始圖片；同名輸出會自動加上 (1)、(2)…。
- 輸入上限 4.5 MP（例如 3000 x 1500），輸出上限 75 MP；WebP 單邊上限 16383 px。
- 不支援 16 位元、HDR、動畫圖片。


Usage (English)
---------------
Extract the whole folder anywhere (keep the path short) and run AnimeRestoreX.exe.
Add images or a folder (or drag them onto the list), choose upscale (1x = denoise
only, 2x, 4x) and denoise level, the output folder, format and quality, then press
Start (Ctrl+Enter). Tools > Settings: tile size, language.
Source images are never modified; existing output files are never overwritten.


移除 / Uninstall
----------------
刪除整個資料夾即可。設定（語言、資料夾記憶、降噪 / tile、視窗大小）存於
HKEY_CURRENT_USER\Software\Kaido-MSVC\AnimeRestoreX，如需一併清除可刪除該機碼。


授權 / License
--------------
- AnimeRestoreX: MIT License, Copyright (c) 2026 Kaido-MSVC（LICENSE）
- 第三方元件（Qt 6.12.0 LGPLv3、waifu2x-ncnn-vulkan、ncnn、模型、Visual C++ 執行階段等）
  各自適用原授權，詳見 THIRD_PARTY_NOTICES.md 與 licenses\ 資料夾。
- Qt 6.12.0 對應原始碼（qtbase / qtimageformats / qttranslations，未修改）
  與本 zip 一同發布於同一位置；檔名與 SHA256 見 THIRD_PARTY_NOTICES.md §2。
- 各檔案的 SHA256 見 MANIFEST.txt。
