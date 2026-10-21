import argparse
from concurrent.futures import ThreadPoolExecutor
import csv
import json
import math
from pathlib import Path
import re
import subprocess

from scipy import stats


parser = argparse.ArgumentParser()
parser.add_argument("binary", type=Path)
parser.add_argument("--assets", type=Path,
                    default=Path("../mos-correlation/assets"))
parser.add_argument("--workers", type=int, default=6)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
rows = list(csv.DictReader(
    (args.assets / "CID22_validation_set.csv").open()
))


def score(row):
    paths = [args.assets / "CID22_validation_set" / row[key]
             for key in ("reference_img", "distorted_img")]
    result = subprocess.run(
        [str(args.binary.resolve()), "ssimu2", *map(str, paths)],
        capture_output=True, text=True, check=True,
    )
    values = re.findall(r"^\s*(-?\d+(?:\.\d+)?)\s*$",
                        result.stdout + "\n" + result.stderr, re.MULTILINE)
    if len(values) != 1 or not math.isfinite(float(values[0])):
        raise ValueError(f"Invalid score for {paths}: {result}")
    return float(values[0])


with ThreadPoolExecutor(max_workers=args.workers) as pool:
    scores = list(pool.map(score, rows))
truth = [float(row["MCOS"]) for row in rows]
result = {
    "pairs": len(rows),
    "KRCC": float(stats.kendalltau(truth, scores).statistic),
    "SRCC": float(stats.spearmanr(truth, scores).statistic),
    "PCC": float(stats.pearsonr(truth, scores).statistic),
}
print(json.dumps(result, indent=2))
args.output.write_text(json.dumps({**result, "scores": scores}, indent=2))
