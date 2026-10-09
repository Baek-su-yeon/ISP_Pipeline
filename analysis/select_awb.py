"""AWB 평가용 manifest: cam_mul이 유효한 전체 (기준 조명을 cam_mul로 잡을 수 있는 사진).

실행 (레포 루트에서):
    python analysis/select_awb.py
    python analysis/select_awb.py --index data/index.csv --out experiments/02_awb/manifest.csv
"""

import argparse
from datetime import date

from manifest_io import DEFAULT_INDEX, compute_missing_paths, load_index, save_manifest

DEFAULT_OUT = "experiments/02_awb/manifest.csv"


def compute_awb_manifest(index):
    """cam_mul_valid인 사진 전체. 돌려주는 값: (manifest, 제외한 장수)."""
    valid = index[index["cam_mul_valid"]]
    return valid[["id", "path"]], len(index) - len(valid)


def main():
    parser = argparse.ArgumentParser(description="AWB 평가용 manifest를 만든다")
    parser.add_argument("--index", default=DEFAULT_INDEX, help="색인 csv")
    parser.add_argument("--out", default=DEFAULT_OUT, help="출력 manifest csv")
    args = parser.parse_args()

    index = load_index(args.index)
    manifest, n_excluded = compute_awb_manifest(index)
    missing = compute_missing_paths(manifest)
    comment = f"all with valid cam_mul, n={len(manifest)}, excluded={n_excluded}, date={date.today().isoformat()}"
    save_manifest(manifest, args.out, [comment])
    print(f"manifest {len(manifest)}장, 제외 {n_excluded}장 -> {args.out} (DNG 파일 없음 {len(missing)}장 {missing[:10]})")


if __name__ == "__main__":
    main()
