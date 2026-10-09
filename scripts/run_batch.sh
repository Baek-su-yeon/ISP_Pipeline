#!/usr/bin/env bash
# manifest의 사진마다 ./build/isp를 돌려 사진당 csv 한 줄을 모으는 배치 실행 스크립트.
#
# 사용법 (항상 레포 루트에서):
#   scripts/run_batch.sh <manifest.csv> <실험폴더> -- <isp 옵션...>
#   예: scripts/run_batch.sh experiments/_dryrun/manifest.csv experiments/_dryrun -- --compare-libraw
#
# - manifest: 열 id,path. '#'로 시작하는 줄과 헤더(id,path)는 건너뜀
# - 사진마다 ./build/isp <path> <옵션> --no-save --csv <실험폴더>/rows/<id>.csv 를 실행
# - 이어서 실행: rows/<id>.csv가 이미 있으면 건너뜀. 중간에 멈췄다면 다시 돌리면 남은 것만 실행
# - 한 장이 실패해도 전체는 계속. 끝나면 rows/*.csv를 manifest 순서로 합쳐 results.csv를 만들고
#   run_info.txt를 쓴다
# - 동시 실행 수: 환경 변수 JOBS (기본 = 코어 수 - 1). 예: JOBS=4 scripts/run_batch.sh ...
#   --compare-libraw는 한 장당 메모리를 약 0.6~0.7GB 쓰므로 JOBS * 0.7GB가 RAM 안에 들어오게 한다

set -u

usage() {
    sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 1
}

[ $# -ge 3 ] || usage
manifest="$1"
exp_dir="${2%/}"
shift 2
[ "$1" = "--" ] || usage
shift
isp_opts=("$@")   # '--' 뒤의 isp 옵션 (비어 있어도 됨)

isp=./build/isp
[ -x "$isp" ] || { echo "build/isp 가 없습니다. 레포 루트에서 cmake --build build 를 먼저 하세요" >&2; exit 1; }
[ -f "$manifest" ] || { echo "manifest 가 없습니다: $manifest" >&2; exit 1; }

jobs="${JOBS:-$(( $(nproc) - 1 ))}"
[ "$jobs" -ge 1 ] 2>/dev/null || jobs=1

rows_dir="$exp_dir/rows"
mkdir -p "$rows_dir"

# manifest → "id<TAB>path" 목록 (주석·헤더·빈 줄 제외, 같은 id는 처음 것만)
list=$(awk -F, '
    /^[[:space:]]*#/ || /^[[:space:]]*$/ { next }
    $1 == "id" && $2 == "path"           { next }
    !seen[$1]++ { p = $0; sub(/^[^,]*,/, "", p); printf "%s\t%s\n", $1, p }
' "$manifest")

total=$(printf '%s\n' "$list" | grep -c .)
[ "$total" -gt 0 ] || { echo "manifest 에 사진이 없습니다: $manifest" >&2; exit 1; }

# 아직 rows 파일이 없는 사진만 실행 대상
pending=""
skipped=0
while IFS=$'\t' read -r id path; do
    if [ -s "$rows_dir/$id.csv" ]; then
        skipped=$((skipped + 1))
    else
        pending+="$id"$'\t'"$path"$'\n'
    fi
done <<< "$list"
n_pending=$((total - skipped))

start_time=$(date '+%Y-%m-%d %H:%M:%S')
start_sec=$(date +%s)
echo "전체 $total 장, 이미 끝난 것 $skipped 장 건너뜀, 실행 $n_pending 장 (동시 $jobs 개)"

# 한 장 실행. 실패해도 0으로 끝낸다 (실패는 그 장의 csv 줄 status=fail 로 남는다)
# 표준 출력은 버리고, 표준 에러는 rows/<id>.err 에 두되 비어 있으면 지운다
run_one() {
    local id path
    IFS=$'\t' read -r id path <<< "$1"
    "$ISP_BIN" "$path" $ISP_OPTS --no-save --csv "$ISP_ROWS/$id.csv" > /dev/null 2> "$ISP_ROWS/$id.err"
    [ -s "$ISP_ROWS/$id.err" ] || rm -f "$ISP_ROWS/$id.err"
    return 0
}
export -f run_one
export ISP_BIN="$isp" ISP_ROWS="$rows_dir" ISP_OPTS="${isp_opts[*]:-}"

# Ctrl-C 로 멈추면 자식도 같이 멈춘다. rows 파일은 사진이 끝날 때 한 번에 쓰므로 반쪽 줄이 남지 않는다
# (kill 0 은 자기 자신에게도 신호를 보내므로 트랩을 먼저 해제한다)
trap 'trap - INT TERM; echo "중단됨. 같은 명령을 다시 실행하면 남은 것만 실행합니다" >&2; kill 0; exit 130' INT TERM

if [ "$n_pending" -gt 0 ]; then
    printf '%s' "$pending" | xargs -d '\n' -P "$jobs" -n 1 bash -c 'run_one "$1"' _
fi

# rows/*.csv 를 manifest 순서로 합쳐 results.csv (헤더는 한 번)
results="$exp_dir/results.csv"
tmp_results="$results.tmp"
: > "$tmp_results"
header=""
n_rows=0
n_bad_header=0
while IFS=$'\t' read -r id path; do
    f="$rows_dir/$id.csv"
    [ -s "$f" ] || continue
    h=$(head -n 1 "$f" | tr -d '\r')
    if [ -z "$header" ]; then
        header="$h"
        printf '%s\n' "$header" > "$tmp_results"
    elif [ "$h" != "$header" ]; then
        echo "헤더가 다른 rows 파일을 건너뜀: $f" >&2
        n_bad_header=$((n_bad_header + 1))
        continue
    fi
    tail -n +2 "$f" >> "$tmp_results"
    n_rows=$((n_rows + 1))
done <<< "$list"
mv "$tmp_results" "$results"

end_time=$(date '+%Y-%m-%d %H:%M:%S')
elapsed=$(( $(date +%s) - start_sec ))

# 성공·실패 (results.csv 의 status 열은 2번째)
n_ok=$(awk -F, 'NR > 1 && $2 == "ok"' "$results" | wc -l)
n_fail=$(awk -F, 'NR > 1 && $2 != "ok"' "$results" | wc -l)
n_missing=$((total - n_rows))

dirty=no
[ -n "$(git status --porcelain --untracked-files=no 2>/dev/null)" ] && dirty=yes

{
    echo "git commit: $(git rev-parse HEAD 2>/dev/null || echo unknown) (수정된 파일 있음: $dirty)"
    echo "manifest: $manifest"
    echo "options: ${isp_opts[*]:-(none)} --no-save --csv"
    echo "jobs: $jobs"
    echo "start: $start_time"
    echo "end: $end_time"
    echo "elapsed_sec: $elapsed"
    echo "total: $total"
    echo "ok: $n_ok"
    echo "fail: $n_fail"
    echo "missing: $n_missing"
    echo "skipped_in_this_run: $skipped"
} > "$exp_dir/run_info.txt"

echo "끝: 전체 $total, 성공 $n_ok, 실패 $n_fail, 결과 없음 $n_missing (${elapsed}초)"
[ "$n_missing" -eq 0 ] || echo "결과가 없는 사진이 있습니다. isp 가 비정상 종료했을 수 있으니 rows/*.err 를 확인하세요" >&2
echo "results: $results"
echo "run_info: $exp_dir/run_info.txt"
