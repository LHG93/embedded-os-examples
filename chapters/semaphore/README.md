# 세마포어

자원의 남은 개수를 세거나(카운팅), 일이 생겼다는 신호를 보낼 때(바이너리) 쓰는 도구

| 예제 | 내용 | 결과 |
|---|---|---|
| 주차장 (Linux · FreeRTOS) | 자리 3개에 차 6대가 20ms씩 주차 | 세마포어 없이 최대 6대 → 카운팅 세마포어 최대 3대 |
| 진동벨 (FreeRTOS) | 바리스타가 벨을 5번 연달아 울리고 손님이 받음 | 바이너리 1번만 받음(4번 사라짐) → 카운팅 5번 다 받음 |
| 진동벨 (Linux) | 같은 방식, POSIX 세마포어 | 5번 다 받음 (POSIX 세마포어는 늘 횟수를 셈) |

## 실행

```bash
cmake --preset posix && cmake --build --preset posix
./build/posix/chapters/semaphore/semaphore-linux
./build/posix/chapters/semaphore/semaphore-freertos
```

## 포인트

- 카운팅 세마포어 = 남은 자리 수. take로 하나 쓰고 give로 하나 반납. 0이면 take가 기다림
- 바이너리 세마포어 = 울렸다/안 울렸다만 기억. 받기 전에 여러 번 울리면 나머지는 사라짐
- 손님(받는 쪽) 우선순위를 바리스타보다 높이면 바이너리도 5번 다 받음. 벨이 울릴 때마다 손님이 바로 깨어나 받기 때문 (`CUSTOMER_PRI`를 `tskIDLE_PRIORITY + 4`로 바꿔서 확인)
- 뮤텍스와 달리 주인이 없음. 울리는 쪽(give)과 받는 쪽(take)이 달라도 됨. ISR에서는 `xSemaphoreGiveFromISR()`
- 우선순위 상속이 없어서, 자원 보호(잠금)에는 뮤텍스를 씀
- macOS는 `sem_init`(이름 없는 세마포어)을 지원하지 않아서 Linux 예제는 `sem_open` 사용

## 지원 플랫폼

| posix | qemu-mps2 | stm32f4 | arduino-uno |
|---|---|---|---|
| ✓ | ✓ | 예정 | 예정 |
