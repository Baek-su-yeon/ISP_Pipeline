"""노이즈 분석용 manifest: 장수 상위 카메라 N종 × ISO 구간별 k장 (시드 고정 무작위).

cam_mul이 유효한 사진 중에서만 고른다 (노이즈 측정에 WB 단계가 들어가므로).
ISO 구간(iso_bin)은 색인의 열을 쓴다. 경계는 build_index.py의 상수.

실행 (레포 루트에서):
    python analysis/select_noise.py
    python analysis/select_noise.py --n-cameras 1 --k 30 --seed 7
"""

import argparse
from datetime import date

import pandas as pd

from manifest_io import DEFAULT_INDEX, compute_missing_paths, load_index, save_manifest

DEFAULT_OUT = "experiments/03_noise/manifest.csv"

NOISE_N_CAMERAS = 2          # 장수 상위 카메라 모델 수 (1~2)
NOISE_K = 20                 # 카메라 × ISO 구간마다 뽑는 장수
NOISE_SEED = 42              # 무작위 선택 시드
ISO_BIN_ORDER = ["low", "mid", "high"]


def compute_top_cameras(index, n_cameras):
    """cam_mul_valid인 사진 중 장수 상위 n_cameras개 model (많은 순서)."""
    return list(index[index["cam_mul_valid"]]["model"].value_counts().index[:n_cameras])


def compute_noise_manifest(index, n_cameras, k, seed):
    """카메라 × ISO 구간마다 k장(모자라면 전부)을 시드 고정 무작위로 뽑는다.
    돌려주는 값: (manifest, 그룹 요약 DataFrame[model, iso_bin, n_pool, n_selected])."""
    valid = index[index["cam_mul_valid"]]
    cameras = compute_top_cameras(index, n_cameras)
    picked = []
    summary = []
    for model in cameras:
        for iso_bin in ISO_BIN_ORDER:
            pool = valid[(valid["model"] == model) & (valid["iso_bin"] == iso_bin)].sort_values("id")
            chosen = pool.sample(n=min(k, len(pool)), random_state=seed).sort_values("id")
            picked.append(chosen)
            summary.append({"model": model, "iso_bin": iso_bin, "n_pool": len(pool), "n_selected": len(chosen)})
    manifest = pd.concat(picked)
    return manifest[["id", "path"]], pd.DataFrame(summary)


def main():
    parser = argparse.ArgumentParser(description="노이즈 분석용 manifest를 만든다")
    parser.add_argument("--index", default=DEFAULT_INDEX, help="색인 csv")
    parser.add_argument("--out", default=DEFAULT_OUT, help="출력 manifest csv")
    parser.add_argument("--n-cameras", type=int, default=NOISE_N_CAMERAS, help="상위 카메라 모델 수")
    parser.add_argument("--k", type=int, default=NOISE_K, help="카메라 × ISO 구간마다 뽑는 장수")
    parser.add_argument("--seed", type=int, default=NOISE_SEED, help="무작위 선택 시드")
    args = parser.parse_args()

    index = load_index(args.index)
    manifest, summary = compute_noise_manifest(index, args.n_cameras, args.k, args.seed)
    cameras = compute_top_cameras(index, args.n_cameras)
    missing = compute_missing_paths(manifest)

    comments = [f"camera={'+'.join(cameras)}, iso_bins={'/'.join(ISO_BIN_ORDER)}, k={args.k}, seed={args.seed}, "
                f"n={len(manifest)}, date={date.today().isoformat()}"]
    comments += [f"group {r.model} {r.iso_bin}: selected {r.n_selected} of {r.n_pool}" for r in summary.itertuples()]
    save_manifest(manifest, args.out, comments)

    print(summary.to_string(index=False))
    short = summary[summary["n_selected"] < args.k]
    print(f"manifest {len(manifest)}장 -> {args.out} (k={args.k}에 못 미친 그룹 {len(short)}개, DNG 파일 없음 {len(missing)}장)")


if __name__ == "__main__":
    main()
