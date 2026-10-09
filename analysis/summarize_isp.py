"""ISP 전수 검증 집계: 우리 ISP와 LibRaw(조건 맞춤) 결과의 차이를 표와 그림으로 정리한다.

입력: experiments/01_isp_validation/results.csv (--compare-libraw 전수 실행) + data/index.csv
열: id,status,error,time_ms,max8_r/g/b,mae8_r/g/b,max16_r/g/b,n_over1_8

실행 (레포 루트에서):
    python analysis/summarize_isp.py
    python analysis/summarize_isp.py --results experiments/_dryrun/results.csv --out experiments/_dryrun/summary

출력 (--out 아래, 기본 experiments/01_isp_validation):
    status_summary.csv           성공·실패 장수, 실패 원인별 장수
    max8_by_<조건>.csv           조건별 차이 표 (model, pattern, has_black, white, iso_step)
    top_max8.csv                 차이 상위 TOP_N장 (렌더링 대상)
    time_stats.csv               처리 시간 통계
    figures/max8_distribution.png/.csv, figures/time_ms_distribution.png/.csv

max8  = 한 장의 8비트 최대 차이 = max(max8_r, max8_g, max8_b)
max16 = 한 장의 선형 16비트 최대 차이 = max(max16_r, max16_g, max16_b)
mae8  = 한 장의 8비트 평균 절대 차이 = mean(mae8_r, mae8_g, mae8_b)
"""

import argparse
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

from build_index import ISO_STEP_OTHER, ISO_STEPS  # noqa: E402

DEFAULT_RESULTS = "experiments/01_isp_validation/results.csv"
DEFAULT_INDEX = "data/index.csv"
DEFAULT_OUT = "experiments/01_isp_validation"

GROUP_COLUMNS = ["model", "pattern", "has_black", "white", "iso_step"]   # 조건별 표의 기준 열
MAX8_WITHIN = 1            # "차이 N 이내" 비율의 N
MIN_GROUP_N = 30           # 이 장수 미만인 조건 그룹은 small=True로 표시
TOP_N = 10                 # 차이 상위 목록 장수
TIME_N_BINS = 40           # 처리 시간 히스토그램 구간 수
FIG_DPI = 150
ISO_STEP_ORDER = [str(step) for step in ISO_STEPS] + [ISO_STEP_OTHER]


def load_results(path):
    """results.csv 읽기. 실패 행의 빈 지표 칸은 NaN, 빈 error는 빈 문자열."""
    results = pd.read_csv(path, dtype={"error": str})
    results["error"] = results["error"].fillna("")
    return results


def load_index(path):
    """색인 읽기. 'None' 글자를 NaN으로 바꾸지 않고, iso_step은 문자열로 읽는다."""
    return pd.read_csv(path, keep_default_na=False, dtype={"iso_step": str})


def compute_status_summary(results):
    """status별, 실패 원인(error)별 장수. 열: kind, label, n"""
    rows = [{"kind": "total", "label": "all", "n": len(results)}]
    rows += [{"kind": "status", "label": k, "n": int(v)} for k, v in results["status"].value_counts().items()]
    failed = results[results["status"] != "ok"]
    rows += [{"kind": "error", "label": k, "n": int(v)} for k, v in failed["error"].value_counts().items()]
    return pd.DataFrame(rows)


def compute_per_photo(ok):
    """성공한 사진마다 채널을 합친 지표 max8, max16, mae8를 붙인다."""
    out = ok.copy()
    out["max8"] = out[["max8_r", "max8_g", "max8_b"]].max(axis=1)
    out["max16"] = out[["max16_r", "max16_g", "max16_b"]].max(axis=1)
    out["mae8"] = out[["mae8_r", "mae8_g", "mae8_b"]].mean(axis=1)
    return out


def compute_max8_distribution(photos):
    """max8 값별 장수와 누적 비율. 열: max8, n, cum_ratio"""
    counts = photos["max8"].astype(int).value_counts().sort_index()
    full = counts.reindex(range(0, int(counts.index.max()) + 1), fill_value=0)
    return pd.DataFrame({"max8": full.index, "n": full.values, "cum_ratio": full.cumsum().values / len(photos)})


def compute_group_table(photos, column):
    """조건(column)별 차이 표. 열: <column>, n, small, max8_mean, max8_max, ratio_max8_within, ..."""
    group = photos.groupby(column, sort=False)
    table = group.agg(
        n=("id", "size"),
        max8_mean=("max8", "mean"),
        max8_max=("max8", "max"),
        ratio_max8_within=("max8", lambda s: float((s <= MAX8_WITHIN).mean())),
        max16_max=("max16", "max"),
        mae8_mean=("mae8", "mean"),
        n_over1_8_mean=("n_over1_8", "mean"),
    ).reset_index()
    table.insert(2, "small", table["n"] < MIN_GROUP_N)
    if column == "iso_step":
        table[column] = pd.Categorical(table[column], ISO_STEP_ORDER, ordered=True)
        return table.sort_values(column, ignore_index=True)
    return table.sort_values("n", ascending=False, ignore_index=True)


def compute_top_photos(photos, top_n):
    """max8 큰 순서(같으면 n_over1_8, max16 큰 순서) 상위 top_n장."""
    columns = ["id", "model", "pattern", "has_black", "white", "iso", "max8", "max16", "mae8", "n_over1_8"]
    ordered = photos.sort_values(["max8", "n_over1_8", "max16", "id"], ascending=[False, False, False, True])
    return ordered[columns].head(top_n).reset_index(drop=True)


def compute_time_stats(results):
    """time_ms 통계 (성공·실패 모두). 병렬 실행 중 측정한 값이라 단독 실행 시간이 아니다."""
    t = results["time_ms"].astype(float)
    return pd.DataFrame({"stat": ["n", "mean", "median", "p95", "max", "min"],
                         "time_ms": [len(t), t.mean(), t.median(), t.quantile(0.95), t.max(), t.min()]})


def compute_time_histogram(results, n_bins):
    t = results["time_ms"].astype(float)
    n, edges = np.histogram(t, bins=n_bins)
    return pd.DataFrame({"time_ms_low": edges[:-1], "time_ms_high": edges[1:], "n": n})


def plot_max8_distribution(dist, n_total):
    fig, ax = plt.subplots(figsize=(max(5, 0.6 * len(dist) + 2), 4.5))
    bars = ax.bar(dist["max8"].astype(str), dist["n"])
    ax.bar_label(bars)
    ax.set_xlabel("max 8-bit difference per photo (max over R,G,B)")
    ax.set_ylabel("number of photos")
    ax.set_title(f"our ISP vs LibRaw: max8 (n={n_total})")
    return fig


def plot_time_histogram(hist, n_total):
    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.bar(hist["time_ms_low"], hist["n"], width=hist["time_ms_high"] - hist["time_ms_low"], align="edge", edgecolor="white")
    ax.set_xlabel("time per photo (ms, measured during parallel run)")
    ax.set_ylabel("number of photos")
    ax.set_title(f"processing time (n={n_total})")
    return fig


def save_csv(df, path):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    df.to_csv(path, index=False)


def save_figure(fig, path):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(path, dpi=FIG_DPI)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description="ISP 전수 검증 결과를 집계한다")
    parser.add_argument("--results", default=DEFAULT_RESULTS, help="results.csv")
    parser.add_argument("--index", default=DEFAULT_INDEX, help="색인 csv")
    parser.add_argument("--out", default=DEFAULT_OUT, help="출력 폴더")
    args = parser.parse_args()

    results = load_results(args.results)
    index = load_index(args.index)
    out = Path(args.out)

    # 색인과 합치고 행 수 확인
    merged = results.merge(index, on="id", how="left", validate="one_to_one", indicator=True)
    n_no_index = int((merged["_merge"] == "left_only").sum())
    print(f"results {len(results)}행, 합친 뒤 {len(merged)}행, 색인에 없는 id {n_no_index}장")
    merged = merged.drop(columns="_merge")

    # status가 ok가 아닌 행은 집계에서 뺀다
    status = compute_status_summary(results)
    ok = merged[merged["status"] == "ok"]
    print(f"집계 대상(status=ok) {len(ok)}장, 제외 {len(merged) - len(ok)}장")
    save_csv(status, out / "status_summary.csv")
    print(status.to_string(index=False))
    if len(ok) == 0:
        print("성공한 사진이 없어 집계를 마친다")
        return

    photos = compute_per_photo(ok)

    dist = compute_max8_distribution(photos)
    save_figure(plot_max8_distribution(dist, len(photos)), out / "figures" / "max8_distribution.png")
    save_csv(dist, out / "figures" / "max8_distribution.csv")
    within = float((photos["max8"] <= MAX8_WITHIN).mean())
    print(f"max8 <= {MAX8_WITHIN}: {within:.4f} ({int((photos['max8'] <= MAX8_WITHIN).sum())}/{len(photos)}), max8 최댓값 {int(photos['max8'].max())}")

    for column in GROUP_COLUMNS:
        table = compute_group_table(photos, column)
        save_csv(table, out / f"max8_by_{column}.csv")
        print(f"max8_by_{column}.csv: {len(table)}그룹, n<{MIN_GROUP_N} {int(table['small'].sum())}그룹")

    top = compute_top_photos(photos, TOP_N)
    save_csv(top, out / "top_max8.csv")
    print(top.to_string(index=False))

    time_stats = compute_time_stats(results)
    save_csv(time_stats, out / "time_stats.csv")
    time_hist = compute_time_histogram(results, TIME_N_BINS)
    save_figure(plot_time_histogram(time_hist, len(results)), out / "figures" / "time_ms_distribution.png")
    save_csv(time_hist, out / "figures" / "time_ms_distribution.csv")
    print(time_stats.to_string(index=False))


if __name__ == "__main__":
    main()
