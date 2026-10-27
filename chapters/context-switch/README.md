# 컨텍스트 스위칭과 틱

태스크를 바꿀 때 지금 레지스터를 그 태스크 스택에 저장하고(문맥 저장), 다음 태스크 것을 꺼냄(복원). 틱은 주기 타이머 인터럽트로, RTOS 시간의 단위

## FreeRTOS (qemu-mps2 전용)

Cortex-M3 레지스터와 25MHz 시계를 직접 봄

```text
1. 저장된 문맥 (Cortex-M3: 레지스터 16개 = 64바이트)
   만들자마자 : 위치 0x20002bb0, PC 0x000000fc (worker 함수 처음: 예), R0 0x1234abcd (넘긴 인자: 예), xPSR 0x01000000
                LR 0x00002cf5 (함수가 끝나면 갈 곳: 오류 처리), R4 ~ R11 0xa5a5a5a5 (아직 빈 값)
   잠든 뒤    : 위치 0x20002b78 (56바이트 더 씀), PC 0x00001eb0 (커널 안, 기다리던 자리), xPSR 0x61000000
2. 틱 (configTICK_RATE_HZ 1000 → 1틱 = 1000us)
   시계로 100ms 동안 틱 99번
   틱 경계 +  0us에 vTaskDelay(1) → 실제로  970us 기다림
   틱 경계 +300us에 vTaskDelay(1) → 실제로  690us 기다림
   틱 경계 +600us에 vTaskDelay(1) → 실제로  413us 기다림
   틱 경계 +900us에 vTaskDelay(1) → 실제로  147us 기다림
3. 전환 비용 (알림 핑퐁 10000번 = 전환 20000번, 틱 96번 동안)
   전환 한 번 평균 4.83us (QEMU 값, 참고용)
```

저장된 PC가 어느 함수인지는 addr2line으로 확인

```bash
arm-none-eabi-addr2line -f -s -e build/qemu-mps2/chapters/context-switch/context-switch-freertos.elf 0x00001eb0
# ulTaskGenericNotifyTake
# tasks.c:7856
```

- 주소는 빌드마다 다를 수 있음
- QEMU는 PC 시계로 틱 인터럽트를 만들어 조금씩 늦게 오기도 함. 2는 위치마다 15번 재서 가운데 값
- 3의 시간은 QEMU가 PC에서 흉내 낸 값. 실제 칩 속도와 무관

## Linux

```text
1. 전환 횟수 (스스로 비켜 줌 / 뺏김)
   1ms씩 100번 자는 스레드          :  100 / 0
   CPU 하나를 300ms 다투는 스레드 둘 :    0 / 15, 0 / 16
2. 전환 비용 (파이프 핑퐁 50000번, CPU 0 하나)
   왕복 한 번 3.63us → 전환 한 번 약 1.81us (시스템 콜 포함)
3. 시간 해상도 (CLOCK_MONOTONIC 해상도 1ns)
   1ms 잠   → 실제 평균   1545us, 최대   1641us
   100us 잠 → 실제 평균    239us, 최대    248us
```

- 도커 데스크톱 VM(Linux 5.15, CPU 4개)에서 확인. 가상 머신이라 잠이 더 늦게 끝남
- 1과 CPU 고정은 Linux 전용. macOS에서는 2 · 3만 출력

## 실행

```bash
# FreeRTOS: QEMU
cmake --preset qemu-mps2 && cmake --build --preset qemu-mps2
./scripts/run_qemu.sh build/qemu-mps2/chapters/context-switch/context-switch-freertos.elf

# Linux
cmake --preset posix && cmake --build --preset posix
./build/posix/chapters/context-switch/context-switch-linux
```

## 바꿔 보기

| 바꿀 곳 | 결과 |
|---|---|
| FreeRTOS `DELAY` 1 → 2 | 2가 2000 · 1700 · 1400 · 1100us 근처. 최소 1틱을 쉬려면 2를 줌 |
| Linux `pin_cpu0()` 줄 지우기 | 핑퐁 왕복이 약 28us로 8배쯤 느려짐. 다른 CPU의 스레드를 깨우는 비용 |
| Linux `pin_cpu0()` 뒤에 `prctl(PR_SET_TIMERSLACK, 1)` (`<sys/prctl.h>`) | 100us 잠이 약 240 → 160us. 기본 50us 여유(timer slack)를 없앰 |

## 포인트

- Cortex-M3 문맥 = R0 ~ R3, R12, LR, PC, xPSR(예외 진입 때 하드웨어가 저장) + R4 ~ R11(PendSV 처리기가 저장). 태스크 스택에 64바이트
- 새 태스크는 "함수 처음에서 멈췄던 것처럼" 문맥을 꾸며 둠. 첫 실행도 복원으로 시작 (PC = 함수, R0 = 인자, LR = 끝나면 갈 오류 처리)
- `vTaskDelay(n)`은 "n번째 틱까지". n = 1이면 0 ~ 1틱 사이 아무 때나 깨어남
- 전환이 잦으면 그만큼 CPU가 일 대신 전환에 씀. 틱을 너무 빠르게 잡으면 틱 인터럽트만으로도 부담
- Linux는 고해상도 타이머라 틱(HZ)에 묶이지 않음. 100us 잠도 1ms까지 늘어나지 않음

## 지원 플랫폼

| posix | qemu-mps2 | stm32f4 | arduino-uno |
|---|---|---|---|
| Linux 예제만 | ✓ | 예정 | 예정 |
