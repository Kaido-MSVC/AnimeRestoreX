# 授權詢問紀錄

發布 v2.0 之前，需要上游明確答覆的授權問題。每則詢問收到回覆後：把原文貼到「回覆原文」，在「結論」寫下採取的決定與修改的檔案，並更新「狀態」。

> 「問了哪些問題」是依據送出前的草稿與報告整理的。實際送出的文字以連結中的原文為準。

| # | 對象 | 連結 | 發出日期 | 狀態 |
|---|---|---|---|---|
| 1 | nagadomi/nunif（模型授權） | <https://github.com/nagadomi/nunif/discussions/751> | 2026-11-03 | 已回覆／結案（2026-11-03） |
| 2 | microsoft/WindowsML（runtime 散布） | <https://github.com/microsoft/WindowsML/issues/27> | 2026-11-04 | 未回覆；改在 #3 詢問 |
| 3 | microsoft/WindowsAppSDK（runtime 散布、EULA） | <https://github.com/microsoft/WindowsAppSDK/issues/6950> | 2026-11-05 | 部分回覆，EULA 待答（2026-11-06） |

---

## 1. nunif 模型授權

- **連結**：<https://github.com/nagadomi/nunif/discussions/751>
- **發出日期**：2026-11-03
- **狀態**：已回覆／結案（結論見本節最後）
- **對象資產**：nunif Release `0.0.0` 的 `waifu2x_onnx_models_20250502.zip`，其中的 `onnx_models/swin_unet/art/*.onnx`（由 commit `31c0137` 匯出；本專案用 `rewrite_pad.py` 把負數 Pad 改寫成 Slice，權重不變；雜湊見 `models/manifest.json`）。
- **背景**：nunif 倉庫的 `LICENSE` 是 MIT，但權重是以 Release 附件散布，不在倉庫內；zip 裡沒有 LICENSE 或 README；Hugging Face 的 `nunif/waifu2x` 模型卡標示 `agpl-3.0`（詳見 `docs/v2.0-P0-P1報告.md` §5）。

### 問了哪些問題

1. Release `0.0.0` 中的預訓練權重與 ONNX 檔（特別是 `swin_unet/art`）適用哪一種授權？
2. Hugging Face 的 `nunif/waifu2x` 倉庫是否由作者維護？該處的 AGPL-3.0 標示是否也適用於這些權重？
3. 如果允許再散布，需要附上什麼署名或通知？

（草稿原文：`docs/v2.0-P0-P1報告.md` §5.4。）

### 當時等待的決定（已結案，保留備查）

- 是否可以把 14 個 `swin_unet/art` 模型（改寫後的版本）放進 v2.0 發行包一起散布，以及要適用哪一種授權。
- 依照答覆修改 `models/manifest.json` 的 `source.license` 與 `status`、`THIRD_PARTY_NOTICES.md.in` §4、`packaging/README.txt` 的模型段落、「關於」視窗的「授權確認中」，以及 EULA §6；然後用 `tools/package.ps1 -IncludeModels` 產生正式封裝。
- 如果答覆是 AGPL-3.0 或不允許再散布：重新討論散布方式（例如不附模型，或改用其他模型）。

### 回覆原文

- **回覆者**：nagadomi（倉庫 Maintainer／Owner）
- **日期**：2026-11-03T17:56:53Z（UTC）
- **連結**：<https://github.com/nagadomi/nunif/discussions/751#discussioncomment-19380179>

> > Hugging Face page nunif/waifu2x is tagged "agpl-3.0"
>
> That is not affiliated with me.
>
> The license for the models I developed in this repository is the same as the license for this repository.
> The answer is the same as for #718, #720, nagadomi/waifu2x#478.
> It's a permissive license, so as long as you believe you're complying with it, there should be no problem.
>
> Also, the currently distributed ONNX models are slightly outdated and do not include the recently added swin_unet_v3/art or TorchDynamo optimizations. The latest ONNX models (the same one used in unlimited:waifu2x) can be generated with the following command.
>
> ```
> python -m waifu2x.export_onnx -i ./waifu2x/pretrained_models -o waifu2x/onnx_models
> ```

### 結論

- nunif 的 waifu2x 模型採 MIT（與倉庫相同）。
- Hugging Face `nunif/waifu2x` 與作者無關，其 `agpl-3.0` 標籤不適用。
- 散布時照樣附上 nunif 的 `LICENSE` 與 `NOTICE`（`licenses/nunif/`）。
- 模型來源改為自行用 nunif 匯出，不再使用 Release `0.0.0` 的 ONNX zip；匯出流程與換代事項見 `docs/v2.0-交接.md` §6。

這份紀錄只更新文件。上面「當時等待的決定」列出的檔案（manifest、NOTICES、`packaging/README.txt`、「關於」視窗、EULA §6）都還沒改，要等換代的模型定案後一起修改，避免寫上之後又要改一次來源與雜湊。

---

## 2. Windows ML runtime 散布

- **連結**：<https://github.com/microsoft/WindowsML/issues/27>
- **發出日期**：2026-11-04
- **狀態**：未回覆（2026-11-07 確認無任何留言）。同一組問題已改在 microsoft/WindowsAppSDK #6950 詢問，回覆見 §3。v2.0 改用上游 ORT + DirectML 發布，這則詢問不再阻擋發布
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

---

## 3. Windows ML runtime 散布與 EULA（WindowsAppSDK）

- **連結**：<https://github.com/microsoft/WindowsAppSDK/issues/6950>
- **標題**：[Question] Redistribution of onnxruntime.dll / DirectML.dll from Microsoft.Windows.AI.MachineLearning in an unpackaged ZIP app
- **發出日期**：2026-11-05
- **狀態**：部分回覆，EULA 待答（第 1、2 題已答，第 3 題 EULA 待答）
- **對象檔案**：NuGet `Microsoft.Windows.AI.MachineLearning` 的 `onnxruntime.dll` 與 `DirectML.dll`，隨免安裝 ZIP 散布（同 §2）。

### 問了哪些問題

1. 套件中的 `onnxruntime.dll` 與 `DirectML.dll` 是否屬於 Distributable Code，可以放進 ZIP 散布？
2. app 不呼叫任何 Windows ML API 時，只散布這兩個 DLL、不附 `Microsoft.Windows.AI.MachineLearning.dll`，是否為支援的配置？
3. 沒有安裝程式的 ZIP 散布，在第一次啟動、載入 DLL 之前顯示 EULA，是否滿足 §3(b)(ii)？

### 等待哪項決定

- 是否把 runtime 從上游 ORT 1.24.4 + DirectML 1.15.4 換回 Windows ML 套件。v2.0 已用上游 runtime 發布，這則詢問只影響之後的版本。
- 換回的條件是 EULA 不要求使用者同意。如果要求使用者同意，會和 v2.0 移除首次同意畫面的決策衝突，維持上游 runtime。
- 換回時要重做的項目：`dumpbin /dependents` 確認 DLL 依賴、重跑 P0／P1、更新 `fetch-runtime.ps1`、`licenses/`、封裝 allowlist 與「關於」。

### 回覆原文

- **回覆者**：andrewleader（Contributor）
- **日期**：2026-11-06（GitHub 顯示日期；台北時間 2026-11-07 上午收到）
- **連結**：<https://github.com/microsoft/WindowsAppSDK/issues/6950#issuecomment-6699096918>

> Hey @Kaido-MSVC, thanks for asking these questions!
>
> 1. Yes, `onnxruntime.dll` and `DirectML.dll` from the `Microsoft.Windows.AI.MachineLearning` NuGet packages are considered Distributable Code and can be included in your ZIP.
> 2. We do not officially support excluding the `Microsoft.Windows.AI.MachineLearning.dll` binary. It's very likely you can do this, and there probably won't be any issues, however it's not an officially supported scenario, so we can't make any guarantees that doing this won't cause breaking changes in the future in your app. Therefore, before updating to new NuGet packages, I'd recommend thoroughly testing your app.
> 3. We're still working on getting an answer for your EULA question.

本專案的回覆（2026-11-07，<https://github.com/microsoft/WindowsAppSDK/issues/6950#issuecomment-6704446235>）：

> Thanks for the clear answers on 1 and 2 — that helps a lot. I’ll keep `Microsoft.Windows.AI.MachineLearning.dll` in the package to stay within the supported scenario. Looking forward to the EULA answer whenever it’s ready.

### 結論（暫定，EULA 待答）

- `onnxruntime.dll`、`DirectML.dll` 可以放進 ZIP 散布。
- 如果換回 Windows ML，`Microsoft.Windows.AI.MachineLearning.dll` 一起散布，維持在官方支援的配置；更新 NuGet 套件前要完整測試。這推翻 §2「不散布 `Microsoft.Windows.AI.MachineLearning.dll`」的做法。
- EULA 答覆前不換 runtime，也不修改任何程式或封裝檔案。
