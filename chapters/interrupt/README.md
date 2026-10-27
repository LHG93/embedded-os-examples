# 인터럽트 기초

하드웨어가 "지금 처리해"라고 CPU를 부르는 것. 하던 일을 멈추고 처리기(ISR)를 실행한 뒤 돌아옴

## FreeRTOS on Cortex-M3 (qemu-mps2 전용)

CMSDK 타이머 두 개(T0 · T1)의 진짜 인터럽트로 확인. 시간은 25MHz 자유 실행 시계(듀얼 타이머)로 잼

| 예제 | 결과 |
|---|---|
| 1kHz 타이머, 태스크가 100ms 자는 동안 | ISR 99번 |
| 중첩: T0 처리기가 2ms 일하는 도중, 더 급한 T1(0x80) | 16 ~ 20번 끼어듦, T1 최대 지연 200 ~ 450us |
| 중첩 없음: T1이 덜 급함(0xE0) | 0번, T1 최대 지연 2.1 ~ 2.4ms |
| `taskENTER_CRITICAL()` 5ms 동안 | 0xC0 타이머는 막힘(풀자마자 1번), 0x20 타이머는 5번 그대로 실행 |

```bash
cmake --preset qemu-mps2 && cmake --build --preset qemu-mps2 --target interrupt-freertos
./scripts/run_qemu.sh build/qemu-mps2/chapters/interrupt/interrupt-freertos.elf
```

## Linux (사용자 공간)

사용자 프로그램은 인터럽트를 직접 못 받음. 비슷한 것: 시그널

| 예제 | 결과 |
|---|---|
| 1kHz `SIGALRM`, 100ms 일하는 동안 | 처리기 99 ~ 100번 끼어듦 |
| 50ms 동안 `sigprocmask`로 막기 | 막은 동안 0번, 풀자마자 1번 (걸림 표시가 하나라 나머지는 사라짐) |
| `/proc/interrupts` (Linux 전용) | 1초 동안 커널이 처리한 CPU 타이머 인터럽트 수 |

```bash
./build/posix/chapters/interrupt/interrupt-linux
```

## 포인트

- Cortex-M 우선순위는 숫자가 작을수록 급함. 급한 인터럽트는 덜 급한 처리기 도중에도 끼어듦(중첩)
- 처리기 끝에서 원인을 지우지 않으면(`TIMER_INTCLR`) 바로 다시 들어와서 태스크가 영원히 못 돎. 지우는 줄을 빼면 멈춤
- FreeRTOS는 `configMAX_SYSCALL_INTERRUPT_PRIORITY`(여기선 0x40)보다 급한 인터럽트는 막지 않음. 대신 그 처리기에서 FreeRTOS API를 부르면 안 됨. `TIMER1_Handler`에 `xTaskGetTickCountFromISR()`를 넣으면 4번(0x20)에서 `configASSERT` 실패
- 막아 둔 인터럽트 · 시그널은 "걸림" 표시 하나만 남음. 여러 번 왔는지는 모름

## 지원 플랫폼

| posix | qemu-mps2 | stm32f4 | arduino-uno |
|---|---|---|---|
| Linux 시그널 예제 | ✓ | 예정 | 예정 |
