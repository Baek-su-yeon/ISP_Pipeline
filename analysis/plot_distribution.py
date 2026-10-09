"""데이터셋 분포: 막대 그래프 6장, 교차표 3개, Cramér's V 히트맵, 교차표 히트맵 3장.

실행 (레포 루트에서):
    python analysis/plot_distribution.py
    python analysis/plot_distribution.py --index data/index.csv --out experiments/00_dataset

출력 (--out 아래):
    figures/dist_<이름>.png 와 같은 이름의 .csv   (subject, light, location, time, model, iso)
    pattern_black_white.csv    패턴 × Black Level 유무 × White Level 조합별 장수
    cross_light_location.csv   빛 × 위치
    cross_model_iso_step.csv   카메라 모델 × ISO 단계
    figures/cramers_v.png/.csv     주제·빛·위치·시간·카메라(상위 10 + other) 5×5 Cramér's V
    figures/cramers_v_n.csv        각 쌍의 계산에 쓴 장수 (EXCLUDE_LABELS 제외 후)
    figures/heatmap_<행>_<열>.png/.csv   빛×위치, 빛×주제, 빛×카메라 장수 (MIN_CELL_N 미만 칸은 회색)
"""

import argparse
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

from build_index import ISO_STEP_OTHER, ISO_STEPS  # noqa: E402

DEFAULT_INDEX = "data/index.csv"
DEFAULT_OUT = "experiments/00_dataset"

# 라벨 막대 그래프로 그리는 의미 정보 열 (표시 순서)
SEMANTIC_COLUMNS = ["subject", "light", "location", "time"]
TOP_N_MODELS = 10          # 카메라 모델 그래프: 장수 상위 N종 + 기타
OTHER_LABEL = "other"
ISO_N_BINS = 30            # ISO 히스토그램 구간 수 (로그 간격)
ISO_STEP_ORDER = [str(step) for step in ISO_STEPS] + [ISO_STEP_OTHER]
FIG_DPI = 150

# 연관성·교차표 히트맵
ASSOC_VARIABLES = ["subject", "light", "location", "time", "camera"]   # camera = 모델 상위 N + other
EXCLUDE_LABELS = ["None"]  # Cramér's V 계산에서 쌍마다 뺄 라벨 (라벨 없음). 'unknown'은 응답이라 유지
MIN_CELL_N = 30            # 히트맵에서 이 장수 미만인 칸은 회색 (해석에서 제외할 수 있게 구분)
HEATMAP_PAIRS = [("light", "location"), ("light", "subject"), ("light", "camera")]


def load_index(path):
    """색인 읽기. 'None' 글자를 NaN으로 바꾸지 않고, iso_step은 문자열로 읽는다."""
    return pd.read_csv(path, keep_default_na=False, dtype={"iso_step": str})


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
    """카메라 모델 × ISO 단계 (열 순서 100, 200, 400, 800, 1600, other)."""
    table = compute_crosstab(index, "model", "iso_step")
    return table.reindex(columns=ISO_STEP_ORDER + ["total"], fill_value=0)


def compute_camera_group(index, top_n):
    """모델 열을 장수 상위 top_n종 + other로 묶은 Series (이름 camera)."""
    counts = index["model"].value_counts()
    top = list(counts.index[:top_n])
    n_rest = len(counts) - len(top)
    other = f"{OTHER_LABEL} ({n_rest} models)"
    return index["model"].where(index["model"].isin(top), other).rename("camera")


def compute_cramers_v(a, b):
    """두 범주형 Series의 Cramér's V와 계산에 쓴 장수. 한쪽이 1종류뿐이면 V는 NaN."""
    table = pd.crosstab(a, b).to_numpy(dtype=float)
    n = table.sum()
    expected = np.outer(table.sum(axis=1), table.sum(axis=0)) / n
    chi2 = ((table - expected) ** 2 / expected).sum()
    k = min(table.shape) - 1
    v = np.sqrt(chi2 / (n * k)) if k > 0 else np.nan
    return v, int(n)


def compute_cramers_v_matrix(variables):
    """variables: {이름: Series}. 쌍마다 EXCLUDE_LABELS인 행을 빼고 V를 구한다.
    돌려주는 값: (V 행렬, 쌍별 사용 장수 행렬) DataFrame."""
    names = list(variables)
    v_matrix = pd.DataFrame(np.nan, index=names, columns=names)
    n_matrix = pd.DataFrame(0, index=names, columns=names)
    for i, a in enumerate(names):
        for b in names[i:]:
            keep = ~(variables[a].isin(EXCLUDE_LABELS) | variables[b].isin(EXCLUDE_LABELS))
            v, n = compute_cramers_v(variables[a][keep], variables[b][keep])
            v_matrix.loc[a, b] = v_matrix.loc[b, a] = v
            n_matrix.loc[a, b] = n_matrix.loc[b, a] = n
    return v_matrix, n_matrix


def compute_heatmap_table(a, b):
    """a(행) × b(열) 교차표 (합계 포함). 행·열 모두 장수 많은 순서, other는 맨 끝."""
    table = pd.crosstab(a, b, margins=True, margins_name="total")
    body = table.drop(index="total", columns="total")
    row_order = sort_labels(body.sum(axis=1))
    col_order = sort_labels(body.sum(axis=0))
    body = body.loc[row_order, col_order]
    body["total"] = body.sum(axis=1)
    body.loc["total"] = body.sum(axis=0)
    return body


def sort_labels(totals):
    """장수 많은 순서로 라벨을 늘어놓되 other로 시작하는 라벨은 맨 끝."""
    ordered = list(totals.sort_values(ascending=False).index)
    return [x for x in ordered if not str(x).startswith(OTHER_LABEL)] + \
           [x for x in ordered if str(x).startswith(OTHER_LABEL)]


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


def plot_iso_histogram(hist, n_total):
    """ISO 히스토그램(x축 로그)을 그려 figure를 돌려준다."""
    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.bar(hist["iso_low"], hist["n"], width=hist["iso_high"] - hist["iso_low"], align="edge", edgecolor="white")
    ax.set_xscale("log")
    ax.set_xlabel("ISO (log scale)")
    ax.set_ylabel("number of photos")
    ax.set_title(f"iso (n={n_total})")
    return fig


def plot_cramers_v_heatmap(v_matrix, n_total):
    """Cramér's V 행렬 히트맵 (0~1). 칸에 값 표시."""
    names = list(v_matrix.index)
    fig, ax = plt.subplots(figsize=(6.5, 5.5))
    image = ax.imshow(v_matrix.to_numpy(dtype=float), cmap="viridis", vmin=0, vmax=1)
    for i in range(len(names)):
        for j in range(len(names)):
            value = v_matrix.iloc[i, j]
            ax.text(j, i, "nan" if np.isnan(value) else f"{value:.2f}", ha="center", va="center",
                    color="white" if np.isnan(value) or value < 0.6 else "black")
    ax.set_xticks(range(len(names)), names, rotation=30, ha="right")
    ax.set_yticks(range(len(names)), names)
    ax.set_xlabel("variable")
    ax.set_ylabel("variable")
    ax.set_title(f"Cramér's V (n={n_total}, 'None' excluded per pair)")
    fig.colorbar(image, ax=ax, label="Cramér's V")
    return fig


def plot_count_heatmap(table, row_name, col_name, n_total):
    """교차표(합계 행·열 제외해서 그림) 히트맵. 칸에 장수 표시, MIN_CELL_N 미만 칸은 회색."""
    body = table.drop(index="total", columns="total")
    counts = body.to_numpy(dtype=float)
    shown = np.ma.masked_where(counts < MIN_CELL_N, counts)
    cmap = plt.get_cmap("Blues").copy()
    cmap.set_bad("lightgray")
    norm = matplotlib.colors.PowerNorm(gamma=0.5, vmin=0, vmax=counts.max())
    fig, ax = plt.subplots(figsize=(max(5.5, 0.9 * body.shape[1] + 3), max(3.5, 0.6 * body.shape[0] + 2.5)))
    image = ax.imshow(shown, cmap=cmap, norm=norm, aspect="auto")
    for i in range(counts.shape[0]):
        for j in range(counts.shape[1]):
            small = counts[i, j] < MIN_CELL_N
            dark = (not small) and norm(counts[i, j]) > 0.5
            ax.text(j, i, f"{int(counts[i, j])}", ha="center", va="center",
                    color="white" if dark else ("dimgray" if small else "black"))
    ax.set_xticks(range(body.shape[1]), [str(c) for c in body.columns], rotation=30, ha="right")
    ax.set_yticks(range(body.shape[0]), [str(r) for r in body.index])
    ax.set_xlabel(col_name)
    ax.set_ylabel(row_name)
    ax.set_title(f"{row_name} x {col_name} (n={n_total}, gray = n < {MIN_CELL_N})")
    fig.colorbar(image, ax=ax, label="number of photos (sqrt scale)")
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
    hist = compute_iso_histogram(index, ISO_N_BINS)
    save_figure(plot_iso_histogram(hist, n_total), fig_dir / "dist_iso.png")
    save_csv(hist, fig_dir / "dist_iso.csv")

    # 표
    pattern_table = compute_pattern_table(index)
    light_location = compute_crosstab(index, "light", "location")
    model_iso = compute_model_iso_crosstab(index)
    save_csv(pattern_table, out / "pattern_black_white.csv")
    save_csv(light_location, out / "cross_light_location.csv", index=True)
    save_csv(model_iso, out / "cross_model_iso_step.csv", index=True)

    # Cramér's V 히트맵
    variables = {name: index[name] for name in SEMANTIC_COLUMNS}
    variables["camera"] = compute_camera_group(index, TOP_N_MODELS)
    variables = {name: variables[name] for name in ASSOC_VARIABLES}
    v_matrix, v_n = compute_cramers_v_matrix(variables)
    save_figure(plot_cramers_v_heatmap(v_matrix, n_total), fig_dir / "cramers_v.png")
    save_csv(v_matrix.round(4), fig_dir / "cramers_v.csv", index=True)
    save_csv(v_n, fig_dir / "cramers_v_n.csv", index=True)

    # 교차표 히트맵 (빛×위치, 빛×주제, 빛×카메라)
    for row_name, col_name in HEATMAP_PAIRS:
        heat = compute_heatmap_table(variables[row_name], variables[col_name])
        stem = f"heatmap_{row_name}_{col_name}"
        save_figure(plot_count_heatmap(heat, row_name, col_name, n_total), fig_dir / f"{stem}.png")
        save_csv(heat, fig_dir / f"{stem}.csv", index=True)
        n_small = int((heat.drop(index="total", columns="total") < MIN_CELL_N).to_numpy().sum())
        print(f"{stem}: {heat.shape[0] - 1}x{heat.shape[1] - 1} 칸, {MIN_CELL_N}장 미만 {n_small}칸, 합계 {heat.loc['total', 'total']}")
    print(v_matrix.round(3).to_string())

    print(f"model 종류 {index['model'].nunique()}, make+model 조합 {index.groupby(['make', 'model']).ngroups}")
    print(f"pattern × has_black × white 조합 {len(pattern_table)}개")
    print(light_location.to_string())
    print(f"저장: {fig_dir} (막대·히스토그램 6장, V·교차표 히트맵 4장 + csv), {out}/*.csv (표 3개)")


if __name__ == "__main__":
    main()
