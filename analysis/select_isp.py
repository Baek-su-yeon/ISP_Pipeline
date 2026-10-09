"""ISP 전수 검증용 manifest: 메타데이터에 성공한 전체 (색인에 있는 모든 사진).

실행 (레포 루트에서):
    python analysis/select_isp.py
    python analysis/select_isp.py --index data/index.csv --out experiments/01_isp_validation/manifest.csv
"""

import argparse
from datetime import date

from manifest_io import DEFAULT_INDEX, compute_missing_paths, load_index, save_manifest

DEFAULT_OUT = "experiments/01_isp_validation/manifest.csv"


def compute_isp_manifest(index):
    """색인 전체. build_index.py가 메타데이터 실패 사진을 이미 뺐다. 열 id, path."""
    return index[["id", "path"]]


def main():
    parser = argparse.ArgumentParser(description="ISP 검증용 manifest를 만든다")
    parser.add_argument("--index", default=DEFAULT_INDEX, help="색인 csv")
    parser.add_argument("--out", default=DEFAULT_OUT, help="출력 manifest csv")
    args = parser.parse_args()

    index = load_index(args.index)
    manifest = compute_isp_manifest(index)
    missing = compute_missing_paths(manifest)
    save_manifest(manifest, args.out, [f"all (metadata ok), n={len(manifest)}, date={date.today().isoformat()}"])
    print(f"manifest {len(manifest)}장 -> {args.out} (DNG 파일 없음 {len(missing)}장 {missing[:10]})")


if __name__ == "__main__":
    main()
