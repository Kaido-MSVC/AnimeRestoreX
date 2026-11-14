<!-- Release 說明草稿（v2.1.0）。格式比照 v2.0.1；標題：AnimeRestoreX v2.1.0。尚未建立 Release。 -->

AnimeRestoreX v2.1.0 新增 TTA、灰階輸出、保留 ICC／EXIF、底片顆粒等選項。AI 模型、推論引擎（ONNX Runtime 1.24.4 + DirectML 1.15.4）與 Qt 6.12.0 都沒有改變；新選項全部關閉時，沒有色彩描述檔、EXIF 的彩色圖片，處理結果與 v2.0.1 完全相同。

## 新功能

選項都在「工具 → 設定」，預設關閉。

- **TTA（測試時增強）**：每張圖翻轉、旋轉後處理 8 次再平均（與 waifu2x／nunif 的 `--tta` 相同）。處理時間約為 8 倍，記憶體也會增加。
- **輸出灰階**：勾選後所有輸出都轉成灰階。原圖本來就是灰階時，不勾選也會輸出灰階（自動）。
- **保留 EXIF**：把相機、拍攝日期等 EXIF 資料複製到輸出。**EXIF 可能包含拍攝位置等個人資訊，分享輸出前請留意。** 方向一律寫成「正常」，像素尺寸改成輸出尺寸，內嵌的縮圖會移除。PNG、JPEG、WebP 都支援。
- **底片顆粒**：AI 處理後加入類似底片的雜訊（與 nunif 的 `--grain` 相同的公式），強度 0.05–1.00，預設 0.20。同一張圖、同樣設定，加入的顆粒每次都相同。

自動處理（沒有選項）：

- **保留 ICC 色彩描述檔**：原圖內嵌的描述檔原樣寫回輸出，像素不做色彩轉換。以前會先轉成 sRGB 再處理，並且不保留描述檔。
- **PNG／WebP 依 EXIF 方向轉正**：和 JPEG 一樣依方向資訊轉正，預覽、清單縮圖與輸出的方向一致。

## 修正

- PNG、WebP 帶 EXIF 方向資訊時，輸出、預覽與清單縮圖的方向和原圖在看圖軟體中顯示的不同（以前只有 JPEG 會轉正）。
- 補上兩個未翻譯的訊息（寫入輸出檔失敗、底片顆粒強度不合法），繁體中文的「插件」改為「外掛」。

## 已知問題

- **TTA 很慢、很耗記憶體**：處理時間約為 8 倍。最大尺寸的 4× 透明圖（約 72 MP 輸出）開啟 TTA 時，程式約使用 4.4 GB 記憶體（不開約 3.5 GB）。
- **ICC 不做色彩轉換**：不支援色彩管理的看圖軟體，顯示輸出的方式與顯示原圖相同。CMYK、Lab 等描述檔不會寫入輸出，仍照舊轉成 sRGB。
- **含透明的灰階輸出**：PNG 與 WebP 沒有 8 位元「灰階＋透明」的寫法，會存成三個數值相同的色彩通道。
- **EXIF 方向**：只讀 EXIF 的 Orientation；只寫在 XMP 裡的方向不會處理。Windows 檔案總管不顯示 PNG 的 EXIF（Windows 本身不讀），這不影響輸出。
- 編碼中取消要等目前檔案編碼完成；Tile 大小會讓結果略有不同；每批第一張會慢幾秒（與 v2.0 相同）。
- 只在 Windows 11 25H2 + NVIDIA GeForce GTX 1060 6GB 上實測；AMD／Intel GPU、Windows 10 未測試。

## 系統需求

- Windows 11 x64、支援 DirectX 12 的顯示卡（沒有合適的 GPU 時可在設定選擇 CPU）。
- 記憶體：建議 8 GB；**開啟 TTA 處理大圖 4× 時建議 16 GB**。
- 不需要安裝，模型已包含在內。程式未數位簽章。

## 下載與使用

下載 `AnimeRestoreX-2.1.0-win64-with-models.zip`，完整解壓縮後執行 `AnimeRestoreX.exe`。從 v2.0.x 更新時，解壓到新資料夾即可，設定會沿用（新選項預設關閉）。

## 檔案與授權

- `AnimeRestoreX-2.1.0-win64-with-models.zip`：程式與模型
- `SHA256SUMS-AnimeRestoreX-2.1.0-win64-with-models.txt`：SHA-256 校驗值
- `qtbase`、`qtimageformats`、`qttranslations` 的 `-everywhere-src-6.12.0.zip`：Qt 6.12.0 對應原始碼（LGPL），一般使用者不需要下載

AnimeRestoreX 自有程式碼採 MIT；模型採 MIT；Qt 6.12.0 採 LGPLv3；ONNX Runtime、DirectML 與其他第三方元件各依原授權。TTA、底片顆粒等演算法依 nunif（MIT）改寫，出處列在 `THIRD_PARTY_NOTICES.md`。包內附 `LICENSE`、`THIRD_PARTY_NOTICES.md` 與 `licenses/`。

`AnimeRestoreX-2.1.0-win64-with-models.zip` SHA256：
```
e9777e541fc740353c6edc77a2816ca0c3113ff32345896166b9bbacdfa47c06
```

**完整變更：** https://github.com/Kaido-MSVC/AnimeRestoreX/compare/v2.0.1...v2.1.0
