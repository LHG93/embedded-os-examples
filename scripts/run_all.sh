#!/usr/bin/env bash
# 모든 예제 빌드 후 하나씩 실행, 전부 정상 종료(0)인지 확인
# 사용법: ./scripts/run_all.sh [프리셋]   (기본값 posix)
set -euo pipefail

PRESET="${1:-posix}"
cd "$(dirname "$0")/.."

cmake --preset "$PRESET" > /dev/null
cmake --build --preset "$PRESET"

# 멈춘 예제가 있어도 끝나게 20초 제한 (macOS엔 timeout이 없어 perl로 대신)
run_limited() {
    if command -v timeout > /dev/null; then timeout 20 "$@"; else perl -e 'alarm 20; exec @ARGV' "$@"; fi
}

fail=0
while IFS= read -r exe; do
    echo "== ${exe#build/$PRESET/chapters/}"
    if run_limited "$exe"; then
        echo "   통과"
    else
        echo "   실패 (종료 코드 $?)"
        fail=1
    fi
done < <(find "build/$PRESET/chapters" -type f -perm -u+x | sort)

exit $fail
