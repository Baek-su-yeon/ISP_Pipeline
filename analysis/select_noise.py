"""노이즈 분석용 manifest: 고정한 카메라 × ISO 단계(100·200·400·800·1600)별 k장 (시드 고정 무작위).

cam_mul이 유효한 사진 중에서만 고른다 (노이즈 측정에 WB 단계가 들어가므로).
ISO 단계(iso_step)는 색인의 열이고, 'other'(그 외 ISO)는 뽑지 않는다.
카메라는 다섯 ISO 단계 모두에 40장 이상 있는 모델 (cam_mul_valid 기준 교차표로 확인).

실행 (레포 루트에서):
    python analysis/select_noise.py
    python analysis/select_noise.py --cameras "EOS 5D" --k 30 --seed 7
"""

import argparse
from datetime import date

import pandas as pd

from build_index import ISO_STEPS
from manifest_io import DEFAULT_INDEX, compute_missing_paths, load_index, save_manifest

DEFAULT_OUT = "experiments/03_noise/manifest.csv"

NOISE_CAMERAS = ["EOS 5D", "EOS 20D"]   # 다섯 ISO 단계 모두 40장 이상인 카메라 (색인의 model 값)
NOISE_K = 10                 # 카메라 × ISO 단계마다 뽑는 장수
NOISE_SEED = 42              # 무작위 선택 시드
ISO_STEP_ORDER = [str(step) for step in ISO_STEPS]   # 'other'는 뽑지 않는다


def compute_noise_manifest(index, cameras, k, seed):
    """카메라 × ISO 단계마다 k장(모자라면 전부)을 시드 고정 무작위로 뽑는다.
    돌려주는 값: (manifest, 그룹 요약 DataFrame[model, iso_step, n_pool, n_selected])."""
    valid = index[index["cam_mul_valid"]]
    picked = []
    summary = []
    for model in cameras:
        for iso_step in ISO_STEP_ORDER:
            pool = valid[(valid["model"] == model) & (valid["iso_step"] == iso_step)].sort_values("id")
            chosen = pool.sample(n=min(k, len(pool)), random_state=seed).sort_values("id")
            picked.append(chosen)
            summary.append({"model": model, "iso_step": iso_step, "n_pool": len(pool), "n_selected": len(chosen)})
    manifest = pd.concat(picked)
    return manifest[["id", "path"]], pd.DataFrame(summary)


def main():
    parser = argparse.ArgumentParser(description="노이즈 분석용 manifest를 만든다")
    parser.add_argument("--index", default=DEFAULT_INDEX, help="색인 csv")
    parser.add_argument("--out", default=DEFAULT_OUT, help="출력 manifest csv")
    parser.add_argument("--cameras", nargs="+", default=NOISE_CAMERAS, help="카메라 모델 (색인의 model 값)")
    parser.add_argument("--k", type=int, default=NOISE_K, help="카메라 × ISO 단계마다 뽑는 장수")
    parser.add_argument("--seed", type=int, default=NOISE_SEED, help="무작위 선택 시드")
    args = parser.parse_args()

    index = load_index(args.index)
    manifest, summary = compute_noise_manifest(index, args.cameras, args.k, args.seed)
    missing = compute_missing_paths(manifest)

    comments = [f"camera={'+'.join(args.cameras)}, iso_steps={'/'.join(ISO_STEP_ORDER)}, k={args.k}, seed={args.seed}, "
                f"n={len(manifest)}, date={date.today().isoformat()}"]
    comments += [f"group {r.model} {r.iso_step}: selected {r.n_selected} of {r.n_pool}" for r in summary.itertuples()]
    save_manifest(manifest, args.out, comments)

    print(summary.to_string(index=False))
    short = summary[summary["n_selected"] < args.k]
    print(f"manifest {len(manifest)}장 -> {args.out} (k={args.k}에 못 미친 그룹 {len(short)}개, DNG 파일 없음 {len(missing)}장)")


if __name__ == "__main__":
    main()
