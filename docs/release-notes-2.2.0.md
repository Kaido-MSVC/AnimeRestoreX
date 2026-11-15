<!-- Release 說明草稿（v2.2.0）。格式比照 v2.1.0；標題：AnimeRestoreX v2.2.0。尚未建立 Release。 -->

AnimeRestoreX v2.2.0 把 AI 執行環境改為 Microsoft Windows ML Runtime，並修正處理惡意圖片時的記憶體保護與 EXIF 縮圖清除。AI 模型、Qt 6.12.0 與所有功能都沒有改變；同樣的圖片與設定，處理結果與 v2.1.0 完全相同（下方安全性修正所處理的異常檔案除外）。

## 變更

- **執行環境改為 Microsoft Windows ML Runtime 2.5.96**（內含 ONNX Runtime 1.28.3、DirectML 1.15.4）。只使用其中的 DirectML（GPU）與 CPU 處理，不會從網路下載任何元件。
- **第一次啟動時需要同意 Microsoft 的授權條款**：畫面顯示 Windows ML Runtime 條款的英文原文，勾選並按「我同意」後才會繼續；按「我不同意」、Esc 或關閉視窗，程式就會結束。同意只需要一次（條款改變時才會再問），之後可在「說明 → 關於 AnimeRestoreX → 授權條款」再次查看。

## 安全性修正

- **開啟刻意做壞的圖片時，不會再用掉大量記憶體**：有些圖片檔可以在很小的檔案裡宣稱帶有非常大的資料（例如內嵌的色彩描述檔），以前程式可能會照著配置大量記憶體。現在色彩描述檔最多只讀 32 MB，超過或資料損壞就略過，圖片本身照常處理；檔案裡宣稱的長度和實際檔案不符時，也會在配置記憶體之前就拒絕。預覽與清單縮圖只讀方向資訊，不讀色彩描述檔。
- **保留 EXIF 時，原圖的縮圖一定會移除**：EXIF 裡常內嵌一張原圖的小縮圖。以前遇到結構不正常的 EXIF（縮圖分成好幾段、資料互相重疊、同一個欄位出現兩次等），縮圖資料可能留在輸出檔裡。現在無法確定能完整移除縮圖時，這張圖就不寫入 EXIF。此項只影響開啟「保留 EXIF」（預設關閉）的情況。

## 已知問題

- **Microsoft 尚未回覆 EULA 的詢問**：Windows ML Runtime 散布時要求使用者同意條款的方式，已向 Microsoft 詢問但還沒有回覆；目前採第一次啟動時顯示條款並要求同意。
- Qt 6.12.0 內附的 libpng、zlib 等元件有較新的修補版；Qt 6.12.1 推出後會以 v2.2.1 更新。目前沒有發現本程式會受這些問題影響的使用方式。
- TTA 很慢、很耗記憶體；ICC 不做色彩轉換；含透明的灰階輸出存成三個相同的色彩通道；只讀 EXIF 的方向（與 v2.1 相同）。
- 編碼中取消要等目前檔案編碼完成；Tile 大小會讓結果略有不同；每批第一張會慢幾秒。
- 只在 Windows 11 25H2 + NVIDIA GeForce GTX 1060 6GB 上實測；AMD／Intel GPU、Windows 10 未測試。

## 系統需求

- Windows 11 x64、支援 DirectX 12 的顯示卡（沒有合適的 GPU 時可在設定選擇 CPU）。
- 記憶體：建議 8 GB；開啟 TTA 處理大圖 4× 時建議 16 GB。
- 不需要安裝，模型已包含在內。程式未數位簽章。

## 下載與使用

下載 `AnimeRestoreX-2.2.0-win64-with-models.zip`，完整解壓縮後執行 `AnimeRestoreX.exe`，第一次啟動時同意 Microsoft 的授權條款。從 v2.0.x／v2.1.x 更新時，解壓到新資料夾即可，設定會沿用。

## 檔案與授權

- `AnimeRestoreX-2.2.0-win64-with-models.zip`：程式與模型
- `SHA256SUMS-AnimeRestoreX-2.2.0-win64-with-models.txt`：SHA-256 校驗值
- `qtbase`、`qtimageformats`、`qttranslations` 的 `-everywhere-src-6.12.0.zip`：Qt 6.12.0 對應原始碼（LGPL），一般使用者不需要下載

AnimeRestoreX 自有程式碼採 MIT；模型採 MIT；Qt 6.12.0 採 LGPLv3；Microsoft Windows ML Runtime（`onnxruntime.dll`、`DirectML.dll`、`Microsoft.Windows.AI.MachineLearning.dll`）依 Microsoft 軟體授權條款散布；新增的 zlib 1.4（只用來解壓 PNG 的色彩描述檔）採 zlib License。包內附 `LICENSE`、`THIRD_PARTY_NOTICES.md` 與 `licenses/`。

`AnimeRestoreX-2.2.0-win64-with-models.zip` SHA256：
```
155cc04678f3fb92fda1f3f8c995ed16e1479925192ab73fa1965c0fde486572
```

**完整變更：** https://github.com/Kaido-MSVC/AnimeRestoreX/compare/v2.1.0...v2.2.0
