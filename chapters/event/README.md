# 이벤트

"이 일들이 다 끝났다" · "이 중 하나가 생겼다"를 비트 묶음으로 알리고 기다리는 도구

| 예제 | 내용 | 결과 |
|---|---|---|
| 모두 기다리기 (AND) | 우유 10ms · 포스기 20ms · 머신 30ms에 비트를 세움 | 오픈 담당이 30ms에 깨어남 (비트 0x7) |
| 아무거나 (OR) | 배달 15ms · 매장 40ms에 주문 | 바리스타가 15ms에 깨어남 (배달) |
| 모여서 출발 (Sync) | 직원 셋이 5 · 15 · 25ms에 준비 끝 | 셋 다 25ms에 함께 출발 |
| 비트는 세지 않음 | 같은 비트를 5번 세움 | 받은 횟수 1번 |

- FreeRTOS: `xEventGroupSetBits()` · `xEventGroupWaitBits()` · `xEventGroupSync()`
- Linux: pthread에 이벤트 그룹이 없어서 뮤텍스 + 조건 변수 + 비트로 직접 만듦 (`ev_set` · `ev_wait` · `ev_sync`)

## 실행

```bash
cmake --preset posix && cmake --build --preset posix
./build/posix/chapters/event/event-linux
./build/posix/chapters/event/event-freertos
```

## 포인트

- `xEventGroupWaitBits(그룹, 비트, 끝나면 지우기, 모두 기다리기, 시간)`. 네 번째 인자 하나로 AND · OR이 바뀜
- 비트는 횟수를 세지 않음. 받기 전에 5번 세워도 1. 횟수가 필요하면 카운팅 세마포어(12장) · 큐(16장)
- "끝나면 지우기"를 끄면 비트가 남아서 다음 기다림이 바로 통과함. 예제 4번에서 `pdTRUE`를 `pdFALSE`로 바꾸면 루프가 끝나지 않음
- 기다리는 쪽이 여럿이면 조건 변수는 `pthread_cond_broadcast()`. `ev_sync()`에서 `signal`로 바꾸면 한 명만 깨어나고 나머지는 영원히 기다림
- AUTOSAR OS 이벤트는 확장 태스크에 붙고, `WaitEvent()`는 "하나라도"(OR)만 지원

## 지원 플랫폼

| posix | qemu-mps2 | stm32f4 | arduino-uno |
|---|---|---|---|
| ✓ | 예정 | 예정 | 예정 |
