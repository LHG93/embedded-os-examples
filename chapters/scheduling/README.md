# 스케줄링

누가 CPU를 쓸지 정하는 규칙. RTOS는 우선순위가 높은 쪽이 바로 뺏음(선점), 같은 우선순위끼리는 틱마다 번갈아(타임 슬라이스). Linux 일반 스레드는 nice 가중치만큼 나눠 씀

## FreeRTOS (posix · qemu-mps2 둘 다)

틱마다 그 틱에 CPU를 쓰던 태스크의 글자를 적음. 괄호 안 숫자는 우선순위

```text
틱마다 CPU를 쓰던 태스크 (20틱, 숫자는 우선순위)
1. 선점          L(1) 바쁨, H(3) 3틱 자고 2틱 일함 : LLHHLLLHHLLLHHLLLHHL
2. 타임 슬라이스 A(2) · B(2) 둘 다 바쁨            : BABABABABABABABABABA  (바뀐 횟수 19)
3. 굶주림        H(3)가 쉬지 않음                  : HHHHHHHHHHHHHHHHHHHH  L이 받은 틱 0
                 H(3)가 1틱 일하고 1틱 쉼          : LHLHLHLHLHLHLHLHLHLH  L이 받은 틱 10
```

## Linux

쉬지 않는 스레드 둘을 CPU 0 하나에 고정

```text
쉬지 않는 스레드 둘을 CPU 0에서 1초 동안 돌림
1. 같은 nice 0 · 0  : CPU 50% · 50%, 한 번에 평균 9.4ms · 9.6ms씩 번갈아 돎 (조각 54 · 53개)
2. nice 0 · 5       : CPU 75% · 25% (가중치 1024 : 335 → 75% : 25%)
3. SCHED_FIFO · 일반 : CPU 95% ·  5% (실시간 스레드가 쉬지 않으면 일반은 굶음. 남은 몫은 커널의 RT 제한)
```

- 도커 데스크톱 VM(Linux 5.15, CFS, CPU 4개)에서 확인. 한 번에 도는 길이는 커널 버전 · CPU 수에 따라 다름 (6.6부터는 EEVDF)
- 3은 실시간 우선순위 권한 필요. 없으면 "건너뜀" 출력 후 정상 종료. 권한 얻는 법은 [priority-inversion](../priority-inversion/README.md#linux-예제-조건)과 같음
- Linux 전용 (CPU 고정 · 스레드별 nice). macOS에서는 안내만 출력

## 실행

```bash
cmake --preset posix && cmake --build --preset posix
./build/posix/chapters/scheduling/scheduling-freertos
./build/posix/chapters/scheduling/scheduling-linux
```

## 바꿔 보기

| 바꿀 곳 | 결과 |
|---|---|
| `configUSE_TIME_SLICING` 0 | 2번이 `AAAA…` (바뀐 횟수 0). 같은 우선순위라도 먼저 잡은 쪽이 놓을 때까지 계속 돎 |
| `configUSE_PREEMPTION` 0 (협력형) | 1번에서 멈춤. 바쁜 L이 양보하지 않아 H도 runner도 다시 못 돎 |
| Linux `b.nice = 5` → `10` | 2번이 90% : 10% (가중치 1024 : 110) |

## 포인트

- 선점: 높은 태스크가 깨어나는 그 틱에 바로 CPU를 뺏음. 낮은 태스크는 하던 일이 끝나지 않아도 밀려남
- 타임 슬라이스: 같은 우선순위만 해당. 다른 우선순위끼리는 나눠 쓰지 않음
- 굶주림: 높은 태스크가 쉬지(차단되지) 않으면 낮은 태스크는 영원히 못 돎. 높은 태스크일수록 일이 끝나면 꼭 기다리게 만듦
- 틱 기록은 틱을 한 번만 읽어서 "끝낼지"와 "적을지"를 같은 값으로 정함. 두 번 읽으면 그 사이 틱이 바뀌어 한 칸씩 어긋남
- Linux 일반 스레드(CFS · EEVDF)는 우선순위가 아니라 가중치로 나눔. nice가 낮아도 조금은 돎. 실시간 정책(SCHED_FIFO)은 RTOS처럼 높은 쪽이 다 가져감

## 지원 플랫폼

| posix | qemu-mps2 | stm32f4 | arduino-uno |
|---|---|---|---|
| ✓ | ✓ | 예정 | 예정 |
