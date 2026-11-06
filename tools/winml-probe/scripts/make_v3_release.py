"""Prepares the Release attachment for the swin_unet_v3 models (dev only; uploads nothing).
Historical: reads the pre-P2 "v3_pending" block; the attachment is published and pinned in models/manifest.json.

  python -I make_v3_release.py <manifest.json> <v3 exports dir> <LICENSE> <NOTICE> <out dir> <doc.md>

Writes the conversion notes to <doc.md> from the manifest, then into <out dir>: the zip (unmodified
nunif exports, LICENSE, NOTICE, CONVERSION.md), loose copies of those three texts, SHA256SUMS.txt.
The zip holds the exports, not the processed models, so tools/fetch-models.ps1 keeps one flow:
source hash -> rewrite_pad -> fix_reshape_allowzero -> final hash. Timestamps are fixed.
"""
import hashlib
import json
import shutil
import sys
import zipfile
from pathlib import Path

DATE = (2026, 11, 4, 0, 0, 0)


def sha256(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def conversion_doc(m):
    src, steps = m["v3_pending"]["source"], m["pipeline"]["steps"]
    rows = "\n".join(f"| `{x['file'].split('/')[-1]}` | {x['source_size']:,} | `{x['source_sha256']}` | {x['size']:,} | `{x['sha256']}` |"
                     for x in m["v3_pending"]["models"])
    return f"""# swin_unet_v3/art 模型：來源與轉換說明

這份檔案說明 AnimeRestoreX 附帶的 waifu2x `swin_unet_v3/art` ONNX 模型從哪裡來、改了什麼。本檔由 `tools/winml-probe/scripts/make_v3_release.py` 依 `models/manifest.json` 產生。

## 來源

- 專案：[nagadomi/nunif](https://github.com/nagadomi/nunif)，commit `{src['commit']}`。
- 授權：MIT，附在同目錄的 `LICENSE`、`NOTICE`（取自該 commit）。作者已確認 waifu2x 模型採 MIT：<https://github.com/nagadomi/nunif/discussions/751>。
- 匯出環境：Python 3.13.17、{src['exported_with']}。
- 匯出指令（在 nunif 根目錄，權重用 `python -m waifu2x.download_models` 取得）：

  ```
  python -m waifu2x.export_onnx -i ./waifu2x/pretrained_models -o waifu2x/onnx_models
  ```

- 範圍：只有 1× 與 2×（noise0–3、noise0–3_scale2x、scale2x）。上游沒有 v3 的 4× 權重，`scale1x` 不使用。換 torch 版本重新匯出會得到不同的圖，下方「檔案雜湊」也會改變。

## 壓縮檔內容

`{src['zip_name']}` 內是**未修改的 nunif 匯出檔**（`{src['path_in_zip']}`），另附 `LICENSE`、`NOTICE` 與本檔。轉換在 `tools/fetch-models.ps1` 取得模型時才執行。

## 轉換

兩個步驟，固定依序執行，腳本都在本專案 `tools/winml-probe/scripts/`。**權重沒有更動**，只改運算圖；這兩項修改是為了讓模型能在 ONNX Runtime 1.24.4 + DirectML 1.15.4 上正確執行。

1. `rewrite_pad.py`（SHA-256 `{steps[0]['script_sha256']}`）：DirectML EP 對負數 Pad 會輸出全零，所以把靜態的負數 Pad 改寫成等價的 Slice。`edge` Pad 只在 pads 可證明 ≥ 0 時放行且不改動，其他情況直接停止。
2. `fix_reshape_allowzero.py`（SHA-256 `{steps[1]['script_sha256']}`）：DirectML EP 在 `allowzero=1` 時不解析 Reshape 的 `-1`，執行會失敗，所以把靜態 shape 含 `-1`、不含 `0` 的節點改成 `allowzero=0`。依 ONNX 規格，`allowzero` 只影響 shape 中的 `0`，語意不變。

工具版本：Python {m['pipeline']['python']}、onnx {m['pipeline']['onnx']}、numpy {m['pipeline']['numpy']}。

## 檔案雜湊

| 檔案 | 原始大小（位元組） | 原始 SHA-256 | 轉換後大小（位元組） | 轉換後 SHA-256 |
|---|---|---|---|---|
{rows}

## 自行重現

取得本專案與 nunif 匯出檔後執行下列指令；任一雜湊不符就會停止，不會安裝任何模型。

```
tools\\fetch-models.ps1 -Force -V3Dir <含上述原始檔的資料夾>
```

## 驗證結果

在 Windows 11、GTX 1060 6GB 上，轉換後的模型以 tile 256 與 PyTorch 比對，8 位元輸出最多差 1；整張圖對 nunif CLI 也最多差 1。詳見本專案 `docs/v2.0-P1重做報告.md`。
"""


def main():
    manifest, exports, lic, notice, out, doc = map(Path, sys.argv[1:7])
    m = json.loads(manifest.read_text(encoding="utf-8"))
    src = m["v3_pending"]["source"]
    doc.write_text(conversion_doc(m), encoding="utf-8", newline="\n")
    out.mkdir(parents=True, exist_ok=True)
    side = {"LICENSE": lic, "NOTICE": notice, "CONVERSION.md": doc}
    z = out / src["zip_name"]
    with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as f:
        def add(arc, p):
            info = zipfile.ZipInfo(arc, DATE)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            f.writestr(info, Path(p).read_bytes(), compresslevel=9)
        for e in m["v3_pending"]["models"]:
            name = e["file"].split("/")[-1]
            if sha256(exports / name) != e["source_sha256"]:
                raise SystemExit(f"{name}: not the export recorded in the manifest")
            add(e["file"], exports / name)
        for arc, p in side.items():
            add(arc, p)
    for arc, p in side.items():
        shutil.copyfile(p, out / arc)
    files = [z] + [out / a for a in side]
    (out / "SHA256SUMS.txt").write_text("".join(f"{sha256(p)}  {p.name}\n" for p in files), encoding="utf-8", newline="\n")
    for p in files:
        print(f"{p.name}  {p.stat().st_size}  {sha256(p)}")


if __name__ == "__main__":
    main()
