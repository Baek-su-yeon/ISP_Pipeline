"""마스터 색인 만들기: 의미 정보(FiveK categories.txt) + 메타데이터(data/meta_raw.csv).

실행 (레포 루트에서):
    python analysis/build_index.py
    python analysis/build_index.py --semantic <categories.txt> --meta <meta_raw.csv> \
        --out data/index.csv --report experiments/00_dataset/index_report.txt

categories.txt 형식 (머리글 없음, 쉼표 5열):
    <파일 이름 stem>,<위치>,<시간>,<빛>,<주제>     예: a0001-jmac_DSC1459,outdoor,day,sun_sky,nature
열 순서는 categories_questions.txt가 아니라 값의 종류를 보고 정했다 (아래 SEMANTIC_COLUMNS).
'None'은 라벨이 없는 칸의 글자 그대로의 값이라 NaN으로 바꾸지 않고 그대로 둔다.
"""

import argparse
from datetime import date
from pathlib import Path

import numpy as np
import pandas as pd

DEFAULT_SEMANTIC = "data/fivek/fivek_dataset/categories.txt"
DEFAULT_META = "data/meta_raw.csv"
DEFAULT_OUT = "data/index.csv"
DEFAULT_REPORT = "experiments/00_dataset/index_report.txt"

# categories.txt의 열 이름 (값의 종류로 확인한 순서: outdoor/indoor, day/night, sun_sky/artificial, man_made/people)
SEMANTIC_COLUMNS = ["location", "time", "light", "subject"]
# 기대하는 원본 영어 라벨. 여기에 없는 값이 나오면 보고서에 적는다
SEMANTIC_LABELS = {
    "location": ["outdoor", "indoor", "unknown", "None"],
    "time": ["day", "dusk", "night", "unknown", "None"],
    "light": ["sun_sky", "artificial", "mixed", "None"],
    "subject": ["man_made", "people", "nature", "animals", "abstract", "None"],
}

# ISO 구간 경계. 분포를 본 뒤 정한다 (plot_distribution.py 결과 확인 후 확정)
# 구간: iso <= ISO_LOW_MAX → low, iso <= ISO_MID_MAX → mid, 그 위 → high
ISO_LOW_MAX = 200
ISO_MID_MAX = 800

# ISO 단계: 아래 값과 정확히 같으면 그 값, 아니면 'other' (문자열로 저장)
ISO_STEPS = [100, 200, 400, 800, 1600]
ISO_STEP_OTHER = "other"

# meta_raw.csv에서 색인에 가져가지 않는 열 (결과 csv와 열 이름이 겹침)
META_DROP_COLUMNS = ["status", "error", "time_ms"]
CBLACK_COLUMNS = ["cblack0", "cblack1", "cblack2", "cblack3"]
CAM_MUL_COLUMNS = ["cam_mul_r", "cam_mul_g", "cam_mul_b"]


def load_meta(path):
    """meta_raw.csv 읽기. 빈 error 칸이 NaN이 되는 것 외에는 그대로."""
    return pd.read_csv(path)


def load_semantic(path):
    """categories.txt 읽기. id는 파일 이름의 첫 '-' 앞. 'None' 글자를 NaN으로 바꾸지 않는다."""
    df = pd.read_csv(path, header=None, names=["name"] + SEMANTIC_COLUMNS, keep_default_na=False)
    df.insert(0, "id", df["name"].str.split("-", n=1).str[0])
    return df.drop(columns="name")


def compute_iso_bin(iso):
    """iso(Series) → 'low'/'mid'/'high'. 경계는 파일 위쪽 상수."""
    return pd.Series(
        np.select([iso <= ISO_LOW_MAX, iso <= ISO_MID_MAX], ["low", "mid"], default="high"),
        index=iso.index,
    )


def compute_iso_step(iso):
    """iso(Series) → '100'/'200'/'400'/'800'/'1600'/'other'. 값이 정확히 같을 때만 그 값."""
    return iso.where(iso.isin(ISO_STEPS), -1).astype(int).astype(str).replace("-1", ISO_STEP_OTHER)


def compute_index(meta, semantic):
    """색인 DataFrame과 보고에 필요한 집계(dict)를 돌려준다. 파일을 읽거나 쓰지 않는다."""
    stats = {"n_meta": len(meta), "n_semantic": len(semantic)}
    stats["dup_meta"] = sorted(meta.loc[meta["id"].duplicated(), "id"])
    stats["dup_semantic"] = sorted(semantic.loc[semantic["id"].duplicated(), "id"])

    meta_failed = meta[meta["status"] != "ok"]
    stats["meta_failed"] = meta_failed[["id", "error"]].to_dict("records")
    meta_ok = meta[meta["status"] == "ok"].drop(columns=META_DROP_COLUMNS)

    stats["no_semantic"] = sorted(set(meta_ok["id"]) - set(semantic["id"]))
    stats["no_meta"] = sorted(set(semantic["id"]) - set(meta["id"]))

    index = semantic.merge(meta_ok, on="id", how="right", validate="one_to_one")
    stats["n_index"] = len(index)
    stats["n_merge_expected"] = len(meta_ok)

    index["iso_bin"] = compute_iso_bin(index["iso"])
    index["iso_step"] = compute_iso_step(index["iso"])
    black_sum = index["black"] + index[CBLACK_COLUMNS].sum(axis=1)
    index["has_black"] = black_sum > 0
    cam_mul = index[CAM_MUL_COLUMNS]
    index["cam_mul_valid"] = np.isfinite(cam_mul).all(axis=1) & (cam_mul > 0).all(axis=1)
    stats["cam_mul_invalid"] = sorted(index.loc[~index["cam_mul_valid"], "id"])
    return index, stats


def compute_report(index, stats, semantic):
    """index_report.txt 내용(문자열)."""
    lines = [f"index_report ({date.today().isoformat()})", ""]

    lines += ["[입력 의미 정보 형식]",
              "파일: categories.txt, 머리글 없음, 쉼표 5열: <stem>,<위치>,<시간>,<빛>,<주제>",
              f"열 이름은 값의 종류로 정함: {', '.join(SEMANTIC_COLUMNS)} (categories_questions.txt는 열 순서를 알려 주지 않음)",
              "'None'은 라벨이 없는 칸, 'unknown'은 질문의 'Hard to tell'로 보임 (원본 라벨 그대로 유지)", ""]
    for col in SEMANTIC_COLUMNS:
        counts = semantic[col].value_counts()
        unexpected = [v for v in counts.index if v not in SEMANTIC_LABELS[col]]
        lines.append(f"{col}: " + ", ".join(f"{k}={v}" for k, v in counts.items()))
        if unexpected:
            lines.append(f"  !! 기대 라벨에 없는 값: {unexpected}")
    lines.append("")

    lines += ["[장수]",
              f"meta_raw.csv: {stats['n_meta']}행 (id 중복 {len(stats['dup_meta'])})",
              f"의미 정보: {stats['n_semantic']}행 (id 중복 {len(stats['dup_semantic'])})",
              f"색인: {stats['n_index']}행 (메타데이터 성공 {stats['n_merge_expected']}행과 {'같음' if stats['n_index'] == stats['n_merge_expected'] else '다름 !!'})", ""]

    lines += ["[빠진 값]",
              f"메타데이터 실패 (색인에서 제외): {len(stats['meta_failed'])}장"]
    lines += [f"  {r['id']}: {r['error']}" for r in stats["meta_failed"][:20]]
    lines += [f"의미 정보 없는 사진 (색인에 있고 의미 정보에 없음): {len(stats['no_semantic'])}장 {stats['no_semantic'][:20]}",
              f"메타데이터 없는 의미 정보 id: {len(stats['no_meta'])}개 {stats['no_meta'][:20]}"]
    for col in SEMANTIC_COLUMNS:
        lines.append(f"{col} 라벨 'None': {int((index[col] == 'None').sum())}장")
    lines += [f"cam_mul 무효 (cam_mul_r/g/b 중 0 이하 또는 NaN): {len(stats['cam_mul_invalid'])}장",
              f"  id: {stats['cam_mul_invalid'][:20]}{' ...' if len(stats['cam_mul_invalid']) > 20 else ''}", ""]

    lines += ["[파생 열]",
              f"iso_bin 경계: low <= {ISO_LOW_MAX} < mid <= {ISO_MID_MAX} < high (분포 확인 전 임시값)"]
    lines += [f"  {k}: {v}장" for k, v in index["iso_bin"].value_counts().reindex(["low", "mid", "high"], fill_value=0).items()]
    lines += [f"has_black (black + cblack0..3 > 0): True {int(index['has_black'].sum())}장, False {int((~index['has_black']).sum())}장",
              f"cam_mul_valid: True {int(index['cam_mul_valid'].sum())}장, False {int((~index['cam_mul_valid']).sum())}장"]
    return "\n".join(lines) + "\n"


def save_index(index, path):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    index.to_csv(path, index=False)


def save_report(text, path):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(text, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description="의미 정보와 메타데이터를 합쳐 마스터 색인을 만든다")
    parser.add_argument("--semantic", default=DEFAULT_SEMANTIC, help="categories.txt 경로")
    parser.add_argument("--meta", default=DEFAULT_META, help="meta_raw.csv 경로")
    parser.add_argument("--out", default=DEFAULT_OUT, help="출력 색인 csv")
    parser.add_argument("--report", default=DEFAULT_REPORT, help="출력 보고서 txt")
    args = parser.parse_args()

    meta = load_meta(args.meta)
    semantic = load_semantic(args.semantic)
    index, stats = compute_index(meta, semantic)
    report = compute_report(index, stats, semantic)
    save_index(index, args.out)
    save_report(report, args.report)
    print(report)
    print(f"저장: {args.out} ({len(index)}행), {args.report}")


if __name__ == "__main__":
    main()
