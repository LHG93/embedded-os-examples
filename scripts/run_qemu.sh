#!/usr/bin/env bash
# QEMU mps2-an385(Cortex-M3)에서 예제 하나 실행
# 사용법: ./scripts/run_qemu.sh build/qemu-mps2/chapters/queue/queue-freertos.elf
# 출력은 UART0, 종료 코드는 예제가 세미호스팅으로 넘김. 리셋되면(-no-reboot) QEMU가 끝남
exec qemu-system-arm -M mps2-an385 -cpu cortex-m3 -nographic -monitor none -no-reboot \
    -semihosting-config enable=on,target=native -kernel "$1"
