"""select_*.py가 함께 쓰는 입출력: 색인 읽기, manifest 쓰기.

manifest 형식: 맨 위에 '#'로 시작하는 줄(선택 기준·장수·시드·날짜), 그 다음 'id,path' 머리글과 사진당 한 줄.
scripts/run_batch.sh가 '#' 줄과 머리글을 건너뛴다.
"""

from pathlib import Path

import pandas as pd

DEFAULT_INDEX = "data/index.csv"


def load_index(path):
    """색인 읽기. 'None' 글자를 NaN으로 바꾸지 않는다."""
    return pd.read_csv(path, keep_default_na=False)


def compute_missing_paths(manifest):
    """path가 가리키는 파일이 없는 id 목록 (읽기 전용 확인)."""
    return [row.id for row in manifest.itertuples() if not Path(row.path).is_file()]


def save_manifest(manifest, path, comment_lines):
    """comment_lines를 '# ' 줄로 맨 위에 쓰고 id,path 두 열을 저장한다."""
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as f:
        for line in comment_lines:
            f.write(f"# {line}\n")
        manifest[["id", "path"]].to_csv(f, index=False, lineterminator="\n")
