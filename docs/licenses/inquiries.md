# 授權詢問紀錄

發布 v2.0 之前，需要上游明確答覆的授權問題。每則詢問收到回覆後：把原文貼到「回覆原文」，在「結論」寫下採取的決定與修改的檔案，並更新「狀態」。

> 「問了哪些問題」是依據送出前的草稿與報告整理的。實際送出的文字以連結中的原文為準。

| # | 對象 | 連結 | 發出日期 | 狀態 |
|---|---|---|---|---|
| 1 | nagadomi/nunif（模型授權） | <https://github.com/nagadomi/nunif/discussions/751> | 2026-11-03 | 等待回覆 |
| 2 | microsoft/WindowsML（runtime 散布） | <https://github.com/microsoft/WindowsML/issues/27> | 2026-11-04 | 等待回覆 |

---

## 1. nunif 模型授權

- **連結**：<https://github.com/nagadomi/nunif/discussions/751>
- **發出日期**：2026-11-03
- **狀態**：等待回覆
- **對象資產**：nunif Release `0.0.0` 的 `waifu2x_onnx_models_20250502.zip`，其中的 `onnx_models/swin_unet/art/*.onnx`（由 commit `31c0137` 匯出；本專案用 `rewrite_pad.py` 把負數 Pad 改寫成 Slice，權重不變；雜湊見 `models/manifest.json`）。
- **背景**：nunif 倉庫的 `LICENSE` 是 MIT，但權重是以 Release 附件散布，不在倉庫內；zip 裡沒有 LICENSE 或 README；Hugging Face 的 `nunif/waifu2x` 模型卡標示 `agpl-3.0`（詳見 `docs/v2.0-P0-P1報告.md` §5）。

### 問了哪些問題

1. Release `0.0.0` 中的預訓練權重與 ONNX 檔（特別是 `swin_unet/art`）適用哪一種授權？
2. Hugging Face 的 `nunif/waifu2x` 倉庫是否由作者維護？該處的 AGPL-3.0 標示是否也適用於這些權重？
3. 如果允許再散布，需要附上什麼署名或通知？

（草稿原文：`docs/v2.0-P0-P1報告.md` §5.4。）

### 等待哪項決定

- 是否可以把 14 個 `swin_unet/art` 模型（改寫後的版本）放進 v2.0 發行包一起散布，以及要適用哪一種授權。
- 依照答覆修改 `models/manifest.json` 的 `source.license` 與 `status`、`THIRD_PARTY_NOTICES.md.in` §4、`packaging/README.txt` 的模型段落、「關於」視窗的「授權確認中」，以及 EULA §6；然後用 `tools/package.ps1 -IncludeModels` 產生正式封裝。
- 如果答覆是 AGPL-3.0 或不允許再散布：重新討論散布方式（例如不附模型，或改用其他模型）。

### 回覆原文

（尚未回覆）

### 結論

（待定）

---

## 2. Windows ML runtime 散布

- **連結**：<https://github.com/microsoft/WindowsML/issues/27>
- **發出日期**：2026-11-04
- **狀態**：等待回覆
- **對象檔案**：NuGet `Microsoft.Windows.AI.MachineLearning` 2.5.96 的 `runtimes/win-x64/native/onnxruntime.dll`（ORT 1.28.3）與 `DirectML.dll`（1.15.4），未修改，隨免安裝 ZIP 一起散布。不散布 `Microsoft.Windows.AI.MachineLearning.dll`。
- **條款**：套件內的 `license.txt`（Microsoft Software License Terms – Microsoft Windows ML Runtime，SHA-256 `66395f8c…0c376814`）；條文摘錄與逐條對照見 `docs/v2.0-P5報告.md` §2。

### 問了哪些問題

1. **§3(a)(i)**：條文寫的是「files that are binplaced with your application by the **WindowsAppSDK** NuGet package」。由 `Microsoft.Windows.AI.MachineLearning` 套件放到 app 旁的 `onnxruntime.dll` 與 `DirectML.dll`，是否同樣屬於可再散布的 distributable code？自包含（self-contained）、免安裝 ZIP 的散布方式是否適用？
2. **§3(b)(ii)**：要求「distributors and external end users to agree to terms that protect it and Microsoft at least as much as this agreement」。對於免安裝 ZIP，在第一次啟動時顯示 EULA（全文收錄 Microsoft 條款），使用者勾選並同意後才載入這兩個 DLL，這種 click-through 方式是否足夠？EULA 內容有沒有必須包含的條款？

### 等待哪項決定

- 能否以現行方式（`onnxruntime.dll`、`DirectML.dll` 放進 ZIP）散布 v2.0。
- EULA 是否需要調整：方向是只保留 Microsoft 要求的條款、MIT 與簡短的免責聲明（見交接文件 §3）。
- 如果答覆是不適用 (a)(i)：重新討論 runtime 的取得方式（例如讓使用者另外取得）。

不在這則詢問的範圍內：§3(b)(iii) 的賠償責任由專案作者自行決定。

### 回覆原文

（尚未回覆）

### 結論

（待定）
