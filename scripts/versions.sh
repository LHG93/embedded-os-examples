#!/usr/bin/env bash
# 지금 환경에 설치된 도구 버전 한 번에 보기. 질문할 때 이 출력을 같이 붙이면 좋음
cd "$(dirname "$0")/.."

show() {
    local name=$1 cmd=$2; shift 2
    if command -v "$cmd" > /dev/null; then
        printf '%-20s %s\n' "$name" "$("$cmd" "$@" 2>&1 | head -1)"
    else
        printf '%-20s %s\n' "$name" "(없음)"
    fi
}

if [ -r /etc/os-release ]; then
    printf '%-20s %s\n' "OS" "$(. /etc/os-release && echo "$PRETTY_NAME")"
else
    printf '%-20s %s\n' "OS" "$(uname -sr)"
fi
show "gcc"               gcc --version
show "cmake"             cmake --version
show "ninja"             ninja --version
show "make"              make --version
show "gdb"               gdb --version
show "git"               git --version
show "python3"           python3 --version
show "arm-none-eabi-gcc" arm-none-eabi-gcc --version
show "qemu-system-arm"   qemu-system-arm --version
show "openocd"           openocd --version
printf '%-20s %s\n' "FreeRTOS-Kernel" "$(git -C third_party/FreeRTOS-Kernel describe --tags --always 2>/dev/null || echo '(submodule 없음)')"
