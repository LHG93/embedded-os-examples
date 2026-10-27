#!/usr/bin/env bash
# 모든 예제 빌드 후 하나씩 실행, 전부 정상 종료(0)인지 확인
# 사용법: ./scripts/run_all.sh [프리셋]   (기본값 posix, 보드 흉내는 qemu-mps2)
set -euo pipefail

PRESET="${1:-posix}"
cd "$(dirname "$0")/.."

cmake --preset "$PRESET" > /dev/null
cmake --build --preset "$PRESET"

# 멈춘 예제가 있어도 끝나게 20초 제한 (macOS엔 timeout이 없어 perl로 대신)
# FreeRTOS POSIX 포트가 SIGALRM을 틱으로 쓰므로, 자식 프로세스에 SIGTERM을 보내는 방식
run_limited() {
    if command -v timeout > /dev/null; then
        timeout 20 "$@"
    else
        perl -e 'my $p = fork; if (!$p) { exec @ARGV; exit 127 }
                 $SIG{ALRM} = sub { kill "TERM", $p; sleep 1; kill "KILL", $p };
                 alarm 20; waitpid($p, 0); exit($? & 127 ? 124 : $? >> 8)' "$@"
    fi
}

# qemu-mps2는 .elf를 QEMU로 실행
if [ "$PRESET" = "qemu-mps2" ]; then
    list() { find "build/$PRESET/chapters" -type f -name '*.elf' | sort; }
    runner=./scripts/run_qemu.sh
else
    list() { find "build/$PRESET/chapters" -type f -perm -u+x | sort; }
    runner=
fi

fail=0
while IFS= read -r exe; do
    echo "== ${exe#build/$PRESET/chapters/}"
    if run_limited $runner "$exe"; then
        echo "   통과"
    else
        echo "   실패 (종료 코드 $?)"
        fail=1
    fi
done < <(list)

exit $fail
