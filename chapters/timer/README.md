# 타이머와 알람

정해진 시간 뒤(원샷) 또는 정해진 간격마다(주기) 일을 시키는 방법

## FreeRTOS 소프트웨어 타이머 (posix · qemu-mps2 둘 다)

| 예제 | 결과 |
|---|---|
| 100ms 동안 원샷(50ms) · 주기(20ms) | 원샷 1번, 주기 5번 |
| 콜백을 실행한 태스크 | 타이머 서비스 태스크 `"Tmr Svc"` (우선순위 6 = `configTIMER_TASK_PRIORITY`) |
| 10ms 주기 타이머 옆에 30ms 걸리는 콜백 | 간격 최대 10ms → 40ms |
| 3ms 일 + `vTaskDelay(10)` 10번 vs `vTaskDelayUntil(10)` 10번 | 130ms vs 100ms |

## Linux

| 예제 | 결과 |
|---|---|
| 3ms 일 + `nanosleep(10ms)` 10번 vs 기한을 10ms씩 더하며 자기 | 165 ~ 168ms vs 100 ~ 103ms |
| `timerfd` 10ms 주기 (Linux 전용) | 제때 읽으면 1번, 35ms 늦게 읽으면 한 번에 3번 (놓친 주기 수) |

## 실행

```bash
cmake --preset posix && cmake --build --preset posix
./build/posix/chapters/timer/timer-linux
./build/posix/chapters/timer/timer-freertos
```

## 포인트

- 소프트웨어 타이머 콜백은 인터럽트가 아니라 타이머 서비스(데몬) 태스크에서 차례로 돎. 콜백 하나가 오래 걸리면 다른 타이머가 다 밀림. 콜백 안에서 기다리는 API 금지
- 원샷을 주기로 바꾸려면 `xTimerCreate`의 세 번째 인자를 `pdTRUE`로. 100ms 동안 2번 실행됨
- 주기 작업은 "지금부터 N ms"(상대)가 아니라 "지난 기한부터 N ms"(절대)로 기다려야 밀리지 않음
- Linux의 상대 대기는 일 시간(30ms)에 잠에서 깨는 지연까지 더해져 130ms보다 더 늦음. 절대 기한은 스스로 보정
- AUTOSAR는 카운터 + 알람(`SetRelAlarm` · `SetAbsAlarm`), 여러 시점을 묶은 스케줄 테이블

## 지원 플랫폼

| posix | qemu-mps2 | stm32f4 | arduino-uno |
|---|---|---|---|
| ✓ | ✓ | 예정 | 예정 |
