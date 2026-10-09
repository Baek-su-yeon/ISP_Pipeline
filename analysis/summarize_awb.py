"""AWB 4종 비교 집계: 방법별 각도 오차, 조건별 표, 오차 방향, 교란 확인, 실패 사례를 표와 그림으로 정리한다.

입력: experiments/02_awb/results.csv (--awb-eval 전수 실행) + data/index.csv
열: id,status,error,time_ms,ref_r/g/b,{gw,wp,sog,ge}_r/g/b,{gw,wp,sog,ge}_err,n_blocks,n_sat_blocks
ref_*, <방법>_r/g/b는 G=1로 정규화한 조명 색(illum)이고 <방법>_err는 ref와 추정값 사이의 각도 오차(도)다.

실행 (레포 루트에서):
    python analysis/summarize_awb.py
    python analysis/summarize_awb.py --results experiments/_dryrun_awb/results.csv --out experiments/_dryrun_awb/summary

출력 (--out 아래, 기본 experiments/02_awb):
    status_summary.csv              전체·상태별·실패 원인별 장수
    skip_ids.csv                    status=skip인 id 목록 (ISP 검증의 큰 차이 사진과 대조용)
    err_summary.csv                 방법별 오차: 평균, 중앙값, 최악 25% 평균, 최댓값
    err_pairs.csv                   방법 쌍별 중앙값 차이 + 부트스트랩 95% 구간 (seed, n_boot 기록)
    err_by_<light|subject|location|time>.csv   그룹별 표: 행 = 그룹, 열 = 방법의 중앙값·최악 25%, 장수 n
    direction_median.csv            방법별 dr, db 중앙값
    direction_by_<time>.csv         그룹별·방법별 dr, db 중앙값 (DIRECTION_GROUP_COLUMNS)
    cross_<light|subject>_camera.csv, cramers_v_camera.csv   교란 확인용 교차표와 Cramér's V
    failure_cases.csv               전체·빛별·주제별 방법별 오차 상위 TOP_N장
    figures/err_box_by_light.png/.csv        방법별·빛별 오차 상자 그림
    figures/heatmap_light_subject.png/.csv   빛 × 주제 중앙값 히트맵 (방법별 4칸)
    figures/direction_scatter.png/.csv       방법별 (dr, db) 산점도
    figures/sat_ratio_scatter.png/.csv       포화 블록 비율 대 오차 산점도

정의:
    dr, db  = ln(est_r / ref_r), ln(est_b / ref_b)     (열 이름은 <방법>_dr, <방법>_db)
    worst25 = 방법별 오차가 큰 쪽 상위 25%(올림)장의 평균
    sat_ratio = n_sat_blocks / n_blocks
    est_max_angle = 네 방법의 추정값 (r, 1, b) 벡터 여섯 쌍 중 가장 큰 각도 (도)
    중앙값 차이 = 방법 a의 중앙값 - 방법 b의 중앙값 (median_diff). 사진별 오차 차이(a - b)의
        중앙값은 paired_median_diff로 따로 둔다. 구간은 사진을 복원 추출해서 구한 95% 구간
"""

import argparse
import math
from itertools import combinations
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

from plot_distribution import (  # noqa: E402
    EXCLUDE_LABELS, TOP_N_MODELS, compute_camera_group, compute_cramers_v, compute_heatmap_table)

DEFAULT_RESULTS = "experiments/02_awb/results.csv"
DEFAULT_INDEX = "data/index.csv"
DEFAULT_OUT = "experiments/02_awb"

METHODS = ["gw", "wp", "sog", "ge"]
GROUP_COLUMNS = ["light", "subject", "location", "time"]   # 그룹별 표의 기준 열
FAILURE_GROUP_COLUMNS = ["light", "subject"]               # 실패 사례를 그룹별로 뽑는 기준 열
DIRECTION_GROUP_COLUMNS = ["time"]                         # dr, db 중앙값을 그룹별로 내는 기준 열
CONFOUNDER_PAIRS = [("light", "camera"), ("subject", "camera")]
MIN_GROUP_N = 30           # 이 장수 미만인 그룹·칸은 small=True, 히트맵에서 회색
WORST_FRACTION = 0.25      # "최악 25%"의 비율
TOP_N = 3                  # 그룹별·방법별 실패 사례 장수
BOOTSTRAP_SEED = 42
BOOTSTRAP_N = 10000
BOOTSTRAP_CHUNK = 250      # 한 번에 만드는 복원 추출 횟수 (메모리 때문)
CI_PERCENT = (2.5, 97.5)
FIG_DPI = 150
METHOD_COLORS = {"gw": "tab:blue", "wp": "tab:orange", "sog": "tab:green", "ge": "tab:red"}


def load_results(path):
    """results.csv 읽기. 실패·skip 행의 빈 지표 칸은 NaN, 빈 error는 빈 문자열."""
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
    excluded = results[results["status"] != "ok"]
    rows += [{"kind": "error", "label": f"{status}: {error}", "n": int(n)}
             for (status, error), n in excluded.groupby(["status", "error"]).size().items()]
    return pd.DataFrame(rows)


def compute_skip_ids(merged):
    """status=skip인 사진 목록. 열: id, error, make, model, iso"""
    skipped = merged[merged["status"] == "skip"]
    return skipped[["id", "error", "make", "model", "iso"]].sort_values("id", ignore_index=True)


def compute_angle_deg(r1, b1, r2, b2):
    """G=1 정규화된 두 색 (r1, 1, b1), (r2, 1, b2) 사이의 각도 (도). 작은 각도에서 정밀하도록 atan2 사용."""
    cross_x = 1.0 * b2 - b1 * 1.0
    cross_y = b1 * r2 - r1 * b2
    cross_z = r1 * 1.0 - 1.0 * r2
    cross_norm = np.sqrt(cross_x ** 2 + cross_y ** 2 + cross_z ** 2)
    dot = r1 * r2 + 1.0 + b1 * b2
    return np.degrees(np.arctan2(cross_norm, dot))


def compute_per_photo(ok):
    """성공한 사진마다 dr, db, 포화 비율, 네 방법 추정값끼리의 최대 각도, 카메라 묶음(camera)을 붙인다."""
    out = ok.copy()
    for m in METHODS:
        out[f"{m}_dr"] = np.log(out[f"{m}_r"] / out["ref_r"])
        out[f"{m}_db"] = np.log(out[f"{m}_b"] / out["ref_b"])
    out["sat_ratio"] = out["n_sat_blocks"] / out["n_blocks"]
    angles = [compute_angle_deg(out[f"{a}_r"], out[f"{a}_b"], out[f"{b}_r"], out[f"{b}_b"])
              for a, b in combinations(METHODS, 2)]
    out["est_max_angle"] = np.maximum.reduce(angles)
    out["camera"] = compute_camera_group(out, TOP_N_MODELS)
    return out


def compute_angle_check(photos):
    """csv의 <방법>_err와 ref·추정값에서 다시 계산한 각도의 최대 차이 (도). 열: method, max_abs_diff"""
    rows = []
    for m in METHODS:
        recomputed = compute_angle_deg(photos["ref_r"], photos["ref_b"], photos[f"{m}_r"], photos[f"{m}_b"])
        rows.append({"method": m, "max_abs_diff": float((recomputed - photos[f"{m}_err"]).abs().max())})
    return pd.DataFrame(rows)


def compute_worst_mean(values, fraction):
    """값이 큰 쪽 상위 fraction(올림)장의 평균."""
    arr = np.sort(np.asarray(values, dtype=float))[::-1]
    k = max(1, math.ceil(fraction * len(arr)))
    return float(arr[:k].mean())


def compute_err_summary(photos):
    """방법별 오차 요약. 열: method, n, mean, median, worst25_mean, max"""
    rows = []
    for m in METHODS:
        err = photos[f"{m}_err"]
        rows.append({"method": m, "n": len(err), "mean": err.mean(), "median": err.median(),
                     "worst25_mean": compute_worst_mean(err, WORST_FRACTION), "max": err.max()})
    return pd.DataFrame(rows)


def compute_bootstrap_medians(values, n_boot, seed, chunk):
    """values (사진 × 열)의 사진을 복원 추출해 열별 중앙값을 n_boot번 구한다. 모든 열이 같은 추출을 쓴다(짝지음)."""
    rng = np.random.default_rng(seed)
    n = values.shape[0]
    out = np.empty((n_boot, values.shape[1]))
    for start in range(0, n_boot, chunk):
        size = min(chunk, n_boot - start)
        picks = rng.integers(0, n, size=(size, n))
        out[start:start + size] = np.median(values[picks], axis=1)
    return out


def compute_err_pairs(photos, n_boot, seed, chunk):
    """방법 쌍별 중앙값 차이와 부트스트랩 95% 구간.
    열: method_a, method_b, n, median_diff, ci_low, ci_high, paired_median_diff, paired_ci_low, paired_ci_high, seed, n_boot"""
    errs = photos[[f"{m}_err" for m in METHODS]].to_numpy(dtype=float)
    pairs = list(combinations(range(len(METHODS)), 2))
    # 쌍별 사진별 차이를 열로 붙여 같은 복원 추출로 중앙값을 구한다
    diffs = np.column_stack([errs[:, a] - errs[:, b] for a, b in pairs])
    boot = compute_bootstrap_medians(np.hstack([errs, diffs]), n_boot, seed, chunk)
    boot_median, boot_paired = boot[:, :len(METHODS)], boot[:, len(METHODS):]
    point_median = np.median(errs, axis=0)
    point_paired = np.median(diffs, axis=0)
    rows = []
    for k, (a, b) in enumerate(pairs):
        diff_boot = boot_median[:, a] - boot_median[:, b]
        low, high = np.percentile(diff_boot, CI_PERCENT)
        p_low, p_high = np.percentile(boot_paired[:, k], CI_PERCENT)
        rows.append({"method_a": METHODS[a], "method_b": METHODS[b], "n": len(errs),
                     "median_diff": point_median[a] - point_median[b], "ci_low": low, "ci_high": high,
                     "paired_median_diff": point_paired[k], "paired_ci_low": p_low, "paired_ci_high": p_high,
                     "seed": seed, "n_boot": n_boot})
    return pd.DataFrame(rows)


def compute_group_table(photos, column):
    """그룹(column)별 표. 열: <column>, n, small, <방법>_median, <방법>_worst25 (방법 순서대로)."""
    rows = []
    for label, group in photos.groupby(column, sort=False):
        row = {column: label, "n": len(group), "small": len(group) < MIN_GROUP_N}
        for m in METHODS:
            row[f"{m}_median"] = group[f"{m}_err"].median()
            row[f"{m}_worst25"] = compute_worst_mean(group[f"{m}_err"], WORST_FRACTION)
        rows.append(row)
    return pd.DataFrame(rows).sort_values("n", ascending=False, ignore_index=True)


def compute_heatmap_long(photos, row_column, col_column):
    """row × col 칸마다 장수와 방법별 중앙값. EXCLUDE_LABELS('None')인 행은 뺀다. 열: row, col, n, small, <방법>_median"""
    keep = ~(photos[row_column].isin(EXCLUDE_LABELS) | photos[col_column].isin(EXCLUDE_LABELS))
    rows = []
    for (r, c), group in photos[keep].groupby([row_column, col_column]):
        row = {row_column: r, col_column: c, "n": len(group), "small": len(group) < MIN_GROUP_N}
        for m in METHODS:
            row[f"{m}_median"] = group[f"{m}_err"].median()
        rows.append(row)
    return pd.DataFrame(rows)


def compute_box_stats(photos, column):
    """상자 그림에 쓴 통계 (그룹 'all' + column의 라벨별, 방법별). matplotlib의 boxplot_stats와 같은 값.
    열: group, method, n, min, q1, median, q3, max, whisker_low, whisker_high, n_fliers"""
    groups = [("all", photos)] + [(label, g) for label, g in photos.groupby(column, sort=False)]
    rows = []
    for label, group in groups:
        for m in METHODS:
            err = group[f"{m}_err"].to_numpy()
            s = matplotlib.cbook.boxplot_stats(err)[0]
            rows.append({"group": label, "method": m, "n": len(err), "min": err.min(), "q1": s["q1"],
                         "median": s["med"], "q3": s["q3"], "max": err.max(),
                         "whisker_low": s["whislo"], "whisker_high": s["whishi"], "n_fliers": len(s["fliers"])})
    table = pd.DataFrame(rows)
    order = ["all"] + list(photos[column].value_counts().index)
    table["group"] = pd.Categorical(table["group"], order, ordered=True)
    table["method"] = pd.Categorical(table["method"], METHODS, ordered=True)
    return table.sort_values(["group", "method"], ignore_index=True).astype({"group": str, "method": str})


def compute_direction_median(photos):
    """방법별 dr, db 중앙값. 열: method, n, dr_median, db_median"""
    return pd.DataFrame([{"method": m, "n": len(photos), "dr_median": photos[f"{m}_dr"].median(),
                          "db_median": photos[f"{m}_db"].median()} for m in METHODS])


def compute_direction_group_table(photos, column):
    """그룹(column)별·방법별 dr, db 중앙값. 열: <column>, method, n, small, dr_median, db_median (그룹은 장수 많은 순서)"""
    rows = []
    for label, group in photos.groupby(column, sort=False):
        for m in METHODS:
            rows.append({column: label, "method": m, "n": len(group), "small": len(group) < MIN_GROUP_N,
                         "dr_median": group[f"{m}_dr"].median(), "db_median": group[f"{m}_db"].median()})
    table = pd.DataFrame(rows)
    order = list(photos[column].value_counts().index)
    table[column] = pd.Categorical(table[column], order, ordered=True)
    return table.sort_values(column, kind="stable", ignore_index=True).astype({column: str})


def compute_direction_points(photos):
    """산점도에 쓴 점. 열: id, <방법>_dr, <방법>_db (방법 순서대로)"""
    columns = ["id"] + [f"{m}_{axis}" for m in METHODS for axis in ("dr", "db")]
    return photos[columns].reset_index(drop=True)


def compute_confounder_tables(photos, pairs):
    """교란 확인: 쌍마다 교차표(합계 포함)와 Cramér's V. 'None' 라벨은 쌍마다 뺀다.
    돌려주는 값: ({'<행>_<열>': 교차표}, V 표(열: row, col, cramers_v, n))"""
    crosstabs, rows = {}, []
    for a, b in pairs:
        crosstabs[f"{a}_{b}"] = compute_heatmap_table(photos[a], photos[b])
        keep = ~(photos[a].isin(EXCLUDE_LABELS) | photos[b].isin(EXCLUDE_LABELS))
        v, n = compute_cramers_v(photos[a][keep], photos[b][keep])
        rows.append({"row": a, "col": b, "cramers_v": v, "n": n})
    return crosstabs, pd.DataFrame(rows)


def compute_failure_cases(photos, group_columns, top_n):
    """전체와 그룹별로 방법별 오차 상위 top_n장.
    열: group_by, group, n_group, method, rank, id, <방법>_err, dr, db, light, subject, model, est_max_angle, sat_ratio
    dr, db는 순위를 매긴 방법의 값."""
    scopes = [("all", "all", photos)]
    for column in group_columns:
        scopes += [(column, label, g) for label, g in photos.groupby(column, sort=False)]
    rows = []
    for group_by, label, group in scopes:
        for m in METHODS:
            top = group.sort_values([f"{m}_err", "id"], ascending=[False, True]).head(top_n)
            for rank, (_, p) in enumerate(top.iterrows(), start=1):
                row = {"group_by": group_by, "group": label, "n_group": len(group), "method": m, "rank": rank, "id": p["id"]}
                row.update({f"{k}_err": p[f"{k}_err"] for k in METHODS})
                row.update({"dr": p[f"{m}_dr"], "db": p[f"{m}_db"], "light": p["light"], "subject": p["subject"],
                            "model": p["model"], "est_max_angle": p["est_max_angle"], "sat_ratio": p["sat_ratio"]})
                rows.append(row)
    return pd.DataFrame(rows)


def compute_sat_points(photos):
    """포화 산점도에 쓴 점. 열: id, sat_ratio, <방법>_err"""
    return photos[["id", "sat_ratio"] + [f"{m}_err" for m in METHODS]].reset_index(drop=True)


def plot_err_box(photos, column, n_total):
    """빛(column)별 방법별 오차 상자 그림. 맨 앞 그룹은 전체. 눈금에 그룹 장수 n 표시."""
    labels = ["all"] + list(photos[column].value_counts().index)
    width = 0.8 / len(METHODS)
    fig, ax = plt.subplots(figsize=(max(7, 1.8 * len(labels) + 2), 5.5))
    for k, m in enumerate(METHODS):
        data = [photos[f"{m}_err"].to_numpy()] + \
               [photos.loc[photos[column] == lab, f"{m}_err"].to_numpy() for lab in labels[1:]]
        positions = np.arange(len(labels)) + (k - (len(METHODS) - 1) / 2) * width
        box = ax.boxplot(data, positions=positions, widths=width * 0.9, patch_artist=True,
                         flierprops={"marker": ".", "markersize": 2, "alpha": 0.4, "markeredgecolor": METHOD_COLORS[m]},
                         medianprops={"color": "black"})
        for patch in box["boxes"]:
            patch.set_facecolor(METHOD_COLORS[m])
            patch.set_alpha(0.6)
        ax.plot([], [], color=METHOD_COLORS[m], linewidth=8, alpha=0.6, label=m)
    counts = [n_total] + [int((photos[column] == lab).sum()) for lab in labels[1:]]
    ax.set_xticks(range(len(labels)), [f"{lab}\n(n={n})" for lab, n in zip(labels, counts)])
    ax.set_xlabel(column)
    ax.set_ylabel("angular error (degrees)")
    ax.set_title(f"AWB angular error by {column} (n={n_total}, whiskers 1.5 IQR)")
    ax.legend(title="method", loc="upper right")
    return fig


def plot_heatmap_grid(long, row_column, col_column, n_total):
    """row × col 칸의 방법별 중앙값 히트맵 4칸 (색 범위 공통). n이 MIN_GROUP_N 미만인 칸은 회색, 칸에 중앙값과 n 표시."""
    rows = list(long.groupby(row_column)["n"].sum().sort_values(ascending=False).index)
    cols = list(long.groupby(col_column)["n"].sum().sort_values(ascending=False).index)
    big = long[~long["small"]]
    vmax = max(float(big[[f"{m}_median" for m in METHODS]].to_numpy().max()), 1e-6) if len(big) else 1.0
    cmap = plt.get_cmap("viridis").with_extremes(bad="lightgray")
    fig, axes = plt.subplots(2, 2, figsize=(max(10, 1.6 * len(cols) * 2 + 3), max(7, 0.9 * len(rows) * 2 + 3)),
                             squeeze=False)
    image = None
    for ax, m in zip(axes.ravel(), METHODS):
        values = np.full((len(rows), len(cols)), np.nan)
        counts = np.zeros((len(rows), len(cols)), dtype=int)
        small = np.zeros((len(rows), len(cols)), dtype=bool)
        for rec in long.itertuples():
            i, j = rows.index(getattr(rec, row_column)), cols.index(getattr(rec, col_column))
            values[i, j] = getattr(rec, f"{m}_median")
            counts[i, j] = rec.n
            small[i, j] = rec.small
        image = ax.imshow(np.ma.masked_where(small | np.isnan(values), values), cmap=cmap, vmin=0, vmax=vmax, aspect="auto")
        for i in range(len(rows)):
            for j in range(len(cols)):
                if counts[i, j] == 0:
                    continue
                text = f"n={counts[i, j]}" if small[i, j] else f"{values[i, j]:.1f}\nn={counts[i, j]}"
                color = "dimgray" if small[i, j] else ("white" if values[i, j] < 0.6 * vmax else "black")
                ax.text(j, i, text, ha="center", va="center", fontsize=8, color=color)
        ax.set_xticks(range(len(cols)), cols, rotation=30, ha="right")
        ax.set_yticks(range(len(rows)), rows)
        ax.set_xlabel(col_column)
        ax.set_ylabel(row_column)
        ax.set_title(m)
    fig.suptitle(f"median angular error (degrees) by {row_column} x {col_column} "
                 f"(n={n_total}, gray = n < {MIN_GROUP_N}, 'None' excluded)")
    fig.colorbar(image, ax=axes.ravel().tolist(), label="median angular error (degrees)")
    return fig


def plot_direction_scatter(photos, medians):
    """방법별 (dr, db) 산점도 2×2. 원점 십자선, 빨간 x = 중앙값. 축 범위는 4칸 공통."""
    limit = max(float(photos[[f"{m}_{a}" for m in METHODS for a in ("dr", "db")]].abs().max().max()), 0.1) * 1.05
    fig, axes = plt.subplots(2, 2, figsize=(9, 9), sharex=True, sharey=True)
    for ax, m in zip(axes.ravel(), METHODS):
        ax.scatter(photos[f"{m}_dr"], photos[f"{m}_db"], s=4, alpha=0.25, color=METHOD_COLORS[m], linewidths=0)
        ax.axhline(0, color="black", linewidth=0.8)
        ax.axvline(0, color="black", linewidth=0.8)
        med = medians[medians["method"] == m].iloc[0]
        ax.plot(med["dr_median"], med["db_median"], "x", color="red", markersize=10, markeredgewidth=2,
                label=f"median ({med['dr_median']:.3f}, {med['db_median']:.3f})")
        ax.set_xlim(-limit, limit)
        ax.set_ylim(-limit, limit)
        ax.set_aspect("equal")
        ax.set_title(f"{m} (n={len(photos)})")
        ax.legend(loc="upper right", fontsize=8)
    for ax in axes[1]:
        ax.set_xlabel("dr = ln(est_r / ref_r)")
    for ax in axes[:, 0]:
        ax.set_ylabel("db = ln(est_b / ref_b)")
    fig.suptitle("direction of illuminant estimate error")
    return fig


def plot_sat_scatter(photos):
    """포화 블록 비율(x) 대 각도 오차(y) 산점도 2×2. 축은 4칸 공통."""
    ymax = float(photos[[f"{m}_err" for m in METHODS]].max().max()) * 1.05
    fig, axes = plt.subplots(2, 2, figsize=(9, 8), sharex=True, sharey=True)
    for ax, m in zip(axes.ravel(), METHODS):
        ax.scatter(photos["sat_ratio"], photos[f"{m}_err"], s=4, alpha=0.25, color=METHOD_COLORS[m], linewidths=0)
        ax.set_ylim(0, ymax)
        ax.set_title(f"{m} (n={len(photos)})")
    for ax in axes[1]:
        ax.set_xlabel("saturated block ratio (n_sat_blocks / n_blocks)")
    for ax in axes[:, 0]:
        ax.set_ylabel("angular error (degrees)")
    fig.suptitle("saturated block ratio vs angular error")
    return fig


def save_csv(df, path, index=False):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    df.to_csv(path, index=index)


def save_figure(fig, path):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path, dpi=FIG_DPI, bbox_inches="tight")
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description="AWB 4종 비교 결과를 집계한다")
    parser.add_argument("--results", default=DEFAULT_RESULTS, help="results.csv")
    parser.add_argument("--index", default=DEFAULT_INDEX, help="색인 csv")
    parser.add_argument("--out", default=DEFAULT_OUT, help="출력 폴더")
    parser.add_argument("--seed", type=int, default=BOOTSTRAP_SEED, help="부트스트랩 시드")
    parser.add_argument("--n-boot", type=int, default=BOOTSTRAP_N, help="부트스트랩 반복 수")
    args = parser.parse_args()

    results = load_results(args.results)
    index = load_index(args.index)
    out = Path(args.out)
    fig_dir = out / "figures"

    # 색인과 합치고 행 수 확인
    merged = results.merge(index, on="id", how="left", validate="one_to_one", indicator=True)
    n_no_index = int((merged["_merge"] == "left_only").sum())
    print(f"results {len(results)}행, 합친 뒤 {len(merged)}행, 색인에 없는 id {n_no_index}장")
    merged = merged.drop(columns="_merge")

    # status가 ok가 아닌 행은 집계에서 빼고 이유별 장수를 기록
    status = compute_status_summary(results)
    ok = merged[merged["status"] == "ok"]
    print(f"집계 대상(status=ok) {len(ok)}장, 제외 {len(merged) - len(ok)}장")
    print(status.to_string(index=False))
    save_csv(status, out / "status_summary.csv")
    skip_ids = compute_skip_ids(merged)
    save_csv(skip_ids, out / "skip_ids.csv")
    print(f"skip_ids.csv: {len(skip_ids)}장")
    if len(ok) == 0:
        print("성공한 사진이 없어 집계를 마친다")
        return

    photos = compute_per_photo(ok)
    n = len(photos)
    check = compute_angle_check(photos)
    print("csv의 err와 다시 계산한 각도의 최대 차이(도):", ", ".join(f"{r.method} {r.max_abs_diff:.2e}" for r in check.itertuples()))

    # 방법별 요약, 쌍별 차이
    summary = compute_err_summary(photos)
    save_csv(summary, out / "err_summary.csv")
    print(summary.round(3).to_string(index=False))
    pairs = compute_err_pairs(photos, args.n_boot, args.seed, BOOTSTRAP_CHUNK)
    save_csv(pairs, out / "err_pairs.csv")
    print(f"부트스트랩 시드 {args.seed}, 반복 {args.n_boot}")
    print(pairs.drop(columns=["seed", "n_boot"]).round(3).to_string(index=False))

    # 그룹별 표
    for column in GROUP_COLUMNS:
        table = compute_group_table(photos, column)
        save_csv(table, out / f"err_by_{column}.csv")
        print(f"err_by_{column}.csv: {len(table)}그룹, n<{MIN_GROUP_N} {int(table['small'].sum())}그룹")
        if column == "light":
            print(table.round(2).to_string(index=False))

    # 빛 × 주제 히트맵
    heat = compute_heatmap_long(photos, "light", "subject")
    save_figure(plot_heatmap_grid(heat, "light", "subject", n), fig_dir / "heatmap_light_subject.png")
    save_csv(heat, fig_dir / "heatmap_light_subject.csv")
    print(f"heatmap_light_subject: {len(heat)}칸, n<{MIN_GROUP_N} {int(heat['small'].sum())}칸, 'None' 제외 후 {int(heat['n'].sum())}장")

    # 상자 그림
    save_figure(plot_err_box(photos, "light", n), fig_dir / "err_box_by_light.png")
    save_csv(compute_box_stats(photos, "light"), fig_dir / "err_box_by_light.csv")

    # 오차 방향
    medians = compute_direction_median(photos)
    save_csv(medians, out / "direction_median.csv")
    for column in DIRECTION_GROUP_COLUMNS:
        table = compute_direction_group_table(photos, column)
        save_csv(table, out / f"direction_by_{column}.csv")
        print(f"direction_by_{column}.csv: {table[column].nunique()}그룹")
        print(table.round(3).to_string(index=False))
    save_figure(plot_direction_scatter(photos, medians), fig_dir / "direction_scatter.png")
    save_csv(compute_direction_points(photos), fig_dir / "direction_scatter.csv")
    print(medians.round(4).to_string(index=False))

    # 교란 확인
    crosstabs, cramers = compute_confounder_tables(photos, CONFOUNDER_PAIRS)
    for name, table in crosstabs.items():
        save_csv(table, out / f"cross_{name}.csv", index=True)
    save_csv(cramers, out / "cramers_v_camera.csv")
    print(cramers.round(3).to_string(index=False))

    # 실패 사례와 포화 비율
    failures = compute_failure_cases(photos, FAILURE_GROUP_COLUMNS, TOP_N)
    save_csv(failures, out / "failure_cases.csv")
    print(f"failure_cases.csv: {len(failures)}행 (고유 id {failures['id'].nunique()}장)")
    save_figure(plot_sat_scatter(photos), fig_dir / "sat_ratio_scatter.png")
    save_csv(compute_sat_points(photos), fig_dir / "sat_ratio_scatter.csv")
    print(f"저장: {out} (표), {fig_dir} (그림 4장 + csv)")


if __name__ == "__main__":
    main()
