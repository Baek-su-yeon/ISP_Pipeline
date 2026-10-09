"""데이터셋 분포: 막대 그래프 6장과 교차표 3개.

실행 (레포 루트에서):
    python analysis/plot_distribution.py
    python analysis/plot_distribution.py --index data/index.csv --out experiments/00_dataset

출력 (--out 아래):
    figures/dist_<이름>.png 와 같은 이름의 .csv   (subject, light, location, time, model, iso)
    pattern_black_white.csv    패턴 × Black Level 유무 × White Level 조합별 장수
    cross_light_location.csv   빛 × 위치
    cross_model_iso_bin.csv    카메라 모델 × ISO 구간
"""

import argparse
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

DEFAULT_INDEX = "data/index.csv"
DEFAULT_OUT = "experiments/00_dataset"

# 라벨 막대 그래프로 그리는 의미 정보 열 (표시 순서)
SEMANTIC_COLUMNS = ["subject", "light", "location", "time"]
TOP_N_MODELS = 10          # 카메라 모델 그래프: 장수 상위 N종 + 기타
OTHER_LABEL = "other"
ISO_N_BINS = 30            # ISO 히스토그램 구간 수 (로그 간격)
ISO_BIN_ORDER = ["low", "mid", "high"]
FIG_DPI = 150


def load_index(path):
    """색인 읽기. 'None' 글자를 NaN으로 바꾸지 않는다."""
    return pd.read_csv(path, keep_default_na=False)


def compute_label_counts(index, column):
    """열의 값별 장수. 많은 순서. 열: <column>, n"""
    counts = index[column].value_counts()
    return pd.DataFrame({column: counts.index, "n": counts.values})


def compute_model_counts(index, top_n):
    """카메라 모델별 장수: 상위 top_n종 + 나머지를 other로 합침."""
    counts = compute_label_counts(index, "model")
    top = counts.iloc[:top_n].copy()
    rest = counts.iloc[top_n:]
    if len(rest) > 0:
        top.loc[len(top)] = [f"{OTHER_LABEL} ({len(rest)} models)", int(rest["n"].sum())]
    return top


def compute_iso_histogram(index, n_bins):
    """ISO 로그 간격 히스토그램. 열: iso_low, iso_high, n"""
    iso = index["iso"].astype(float)
    edges = np.logspace(np.log10(iso.min()), np.log10(iso.max()), n_bins + 1)
    n, _ = np.histogram(iso, bins=edges)
    return pd.DataFrame({"iso_low": edges[:-1], "iso_high": edges[1:], "n": n})


def compute_pattern_table(index):
    """패턴 × Black Level 유무 × White Level 조합별 장수."""
    table = index.groupby(["pattern", "has_black", "white"]).size().rename("n").reset_index()
    return table.sort_values("n", ascending=False, ignore_index=True)


def compute_crosstab(index, row, col):
    """row × col 교차표 (합계 포함). 행은 합계가 큰 순서."""
    table = pd.crosstab(index[row], index[col], margins=True, margins_name="total")
    body = table.drop(index="total").sort_values("total", ascending=False)
    return pd.concat([body, table.loc[["total"]]])


def compute_model_iso_crosstab(index):
    """카메라 모델 × ISO 구간 (열 순서 low, mid, high)."""
    table = compute_crosstab(index, "model", "iso_bin")
    return table.reindex(columns=ISO_BIN_ORDER + ["total"], fill_value=0)


def plot_label_bars(counts, column, n_total):
    """라벨별 장수 막대 그래프를 그려 figure를 돌려준다. 막대 위에 장수 표시."""
    fig, ax = plt.subplots(figsize=(max(5, 0.8 * len(counts) + 2), 4.5))
    bars = ax.bar(counts[column].astype(str), counts["n"])
    ax.bar_label(bars)
    ax.set_xlabel(column)
    ax.set_ylabel("number of photos")
    ax.set_title(f"{column} (n={n_total})")
    plt.setp(ax.get_xticklabels(), rotation=30, ha="right")
    return fig


def plot_iso_histogram(hist, n_total, iso_edges):
    """ISO 히스토그램(x축 로그)을 그려 figure를 돌려준다. iso_edges는 iso_bin 경계 (세로 점선)."""
    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.bar(hist["iso_low"], hist["n"], width=hist["iso_high"] - hist["iso_low"], align="edge", edgecolor="white")
    for edge in iso_edges:
        ax.axvline(edge, color="gray", linestyle="--", linewidth=1)
    ax.set_xscale("log")
    ax.set_xlabel("ISO (log scale)")
    ax.set_ylabel("number of photos")
    ax.set_title(f"iso (n={n_total}, dashed = iso_bin edges)")
    return fig


def save_figure(fig, path):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(path, dpi=FIG_DPI)
    plt.close(fig)


def save_csv(df, path, index=False):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    df.to_csv(path, index=index)


def main():
    parser = argparse.ArgumentParser(description="데이터셋 분포 그림과 교차표를 만든다")
    parser.add_argument("--index", default=DEFAULT_INDEX, help="색인 csv")
    parser.add_argument("--out", default=DEFAULT_OUT, help="출력 실험 폴더")
    parser.add_argument("--iso-edges", type=float, nargs="*", default=None,
                        help="ISO 히스토그램에 그릴 iso_bin 경계 (기본: build_index.py의 상수)")
    args = parser.parse_args()

    index = load_index(args.index)
    n_total = len(index)
    out = Path(args.out)
    fig_dir = out / "figures"
    print(f"색인 {n_total}행")

    # 라벨 막대 그래프 (subject, light, location, time, model)
    for column in SEMANTIC_COLUMNS:
        counts = compute_label_counts(index, column)
        save_figure(plot_label_bars(counts, column, n_total), fig_dir / f"dist_{column}.png")
        save_csv(counts, fig_dir / f"dist_{column}.csv")
    model_counts = compute_model_counts(index, TOP_N_MODELS)
    save_figure(plot_label_bars(model_counts, "model", n_total), fig_dir / "dist_model.png")
    save_csv(model_counts, fig_dir / "dist_model.csv")

    # ISO 히스토그램
    iso_edges = args.iso_edges
    if iso_edges is None:
        iso_edges = default_iso_edges()
    hist = compute_iso_histogram(index, ISO_N_BINS)
    save_figure(plot_iso_histogram(hist, n_total, iso_edges), fig_dir / "dist_iso.png")
    save_csv(hist, fig_dir / "dist_iso.csv")

    # 표
    pattern_table = compute_pattern_table(index)
    light_location = compute_crosstab(index, "light", "location")
    model_iso = compute_model_iso_crosstab(index)
    save_csv(pattern_table, out / "pattern_black_white.csv")
    save_csv(light_location, out / "cross_light_location.csv", index=True)
    save_csv(model_iso, out / "cross_model_iso_bin.csv", index=True)

    print(f"model 종류 {index['model'].nunique()}, make+model 조합 {index.groupby(['make', 'model']).ngroups}")
    print(f"pattern × has_black × white 조합 {len(pattern_table)}개")
    print(light_location.to_string())
    print(f"저장: {fig_dir} (그림 6장 + csv), {out}/*.csv (표 3개)")


def default_iso_edges():
    """build_index.py의 iso_bin 경계 상수를 가져온다."""
    from build_index import ISO_LOW_MAX, ISO_MID_MAX
    return [ISO_LOW_MAX, ISO_MID_MAX]


if __name__ == "__main__":
    main()
