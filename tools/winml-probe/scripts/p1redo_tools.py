"""P1 redo helpers (dev only). Inputs and logs are read, models are never modified.

  python -I p1redo_tools.py placement <out.json> <ortlog.txt> [...]
      Summarises ORT "Node placements" from winml_probe --verbose --log files: node count per
      EP and the CPU node list per op, and whether the watched ops reached the CPU.

  python -I p1redo_tools.py hashes <out.json> <rewritten dir> [...]
      Three-way check for every rewrite_pad.py report in the dirs: source, script and
      rewritten file are hashed again from disk and compared with the report.
      Needs the source dir as "<rewritten dir>=<source dir>".

  python -I p1redo_tools.py manifest <base manifest> <out manifest> <models root> <family> <offset table>
      Scratch manifest for tile_runner with the rewritten models of one family. Never
      touches models/manifest.json. <family> e.g. swin_unet_v3/art.
"""
import hashlib
import json
import re
import sys
from collections import Counter
from pathlib import Path

WATCH = ("Gelu", "IsNaN", "Resize", "Pad")
HEAD = re.compile(r"Node\(s\) placed on \[(\w+)\]\. Number of nodes: (\d+)")
NODE = re.compile(r"VerifyEachNodeIsAssignedToAnEp\s+(\w+) \((.*)\)\s*$")


def sha256(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def parse_log(p: Path):
    eps, cur = {}, None
    for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
        m = HEAD.search(line)
        if m:
            cur = m.group(1)
            eps[cur] = dict(count=int(m.group(2)), ops=Counter(), nodes=[])
            continue
        m = NODE.search(line)
        if m and cur:
            eps[cur]["ops"][m.group(1)] += 1
            eps[cur]["nodes"].append(f"{m.group(1)} ({m.group(2)})")
    return eps


def placement(out: Path, logs):
    rows = []
    for log in map(Path, logs):
        eps = parse_log(log)
        cpu = eps.get("CPUExecutionProvider", dict(count=0, ops=Counter(), nodes=[]))
        dml = eps.get("DmlExecutionProvider", dict(count=0, ops=Counter()))
        row = dict(log=log.name, dml_nodes=dml["count"], cpu_nodes=cpu["count"], cpu_ops=dict(cpu["ops"]),
                   dml_ops=dict(dml["ops"]), cpu_node_list=cpu["nodes"],
                   watched_on_cpu={k: cpu["ops"].get(k, 0) for k in WATCH},
                   watched_on_dml={k: dml["ops"].get(k, 0) for k in WATCH})
        rows.append(row)
        print(f"{log.stem:<32} DML {row['dml_nodes']:>5} CPU {row['cpu_nodes']:>4}  watched on CPU "
              f"{row['watched_on_cpu']}  on DML {row['watched_on_dml']}")
        print(f"{'':<32} CPU ops {dict(cpu['ops'].most_common())}")
    out.write_text(json.dumps(rows, indent=1), encoding="utf-8")


def hashes(out: Path, pairs):
    rows, bad = [], 0
    tool = Path(__file__).resolve().parent / "rewrite_pad.py"
    tool_now = sha256(tool)
    for pair in pairs:
        rw, src = map(Path, pair.split("="))
        for rep in sorted(rw.glob("*.json")):
            r = json.loads(rep.read_text(encoding="utf-8"))
            s, d = src / r["src"], rw / r["dst"]
            ok = dict(source=sha256(s) == r["src_sha256"], script=tool_now == r["tool_sha256"],
                      rewritten=sha256(d) == r["dst_sha256"])
            rows.append(dict(model=f"{rw.parent.name}/{rw.name}/{r['src']}", src_sha256=r["src_sha256"],
                             tool_sha256=r["tool_sha256"], dst_sha256=r["dst_sha256"], dst_size=r["dst_size"],
                             **ok, pads=Counter(p["action"] for p in r["pads"])))
            bad += not all(ok.values())
    out.write_text(json.dumps(rows, indent=1), encoding="utf-8")
    print(f"{len(rows)} reports checked against disk, {bad} mismatches; rewrite_pad.py sha256 {tool_now}")
    sys.exit(1 if bad else 0)


def scratch_manifest(base: Path, out: Path, root: Path, family: str):
    m = json.loads(base.read_text(encoding="utf-8"))
    models = []
    for rep in sorted((root / family).glob("*.json")):
        r = json.loads(rep.read_text(encoding="utf-8"))
        n = rep.stem
        s = 4 if "scale4x" in n else 2 if "scale2x" in n else 1
        models.append(dict(id=f"{family}/{n}", file=f"{family}/{r['dst']}", sha256=r["dst_sha256"], scale=s,
                           offset=8 * s, blend_size=4 * s, noise_level=int(n[5]) if n.startswith("noise") else -1))
    ids = {x["id"] for x in models}
    m["alpha"] = {k: f"{family}/{k.split('_')[0]}" for k in ("scale2x_alpha_model", "scale4x_alpha_model")
                  if f"{family}/{k.split('_')[0]}" in ids}
    m["models"] = models
    m["status"] = "SCRATCH - P1 redo validation only"
    out.write_text(json.dumps(m, indent=1), encoding="utf-8")
    print("wrote", out, len(models), "models; alpha", m["alpha"])


def main():
    cmd, a = sys.argv[1], sys.argv[2:]
    if cmd == "placement":
        placement(Path(a[0]), a[1:])
    elif cmd == "hashes":
        hashes(Path(a[0]), a[1:])
    elif cmd == "manifest":
        scratch_manifest(Path(a[0]), Path(a[1]), Path(a[2]), a[3])
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
