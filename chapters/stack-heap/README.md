# 스택과 힙

스택: 함수가 부를 때 쌓고 끝나면 치우는 개인 공간. 태스크마다 하나, 크기는 만들 때 정함
힙: 필요할 때 빌리고 반납하는 공용 공간. 오래 쓰면 조각남(단편화)

## FreeRTOS on Cortex-M3 (qemu-mps2 전용, heap_4)

```text
스택 여유 (2KB 중)  : 가벼운 태스크 1964바이트 남음, 지역 배열 1KB 쓰는 태스크 948바이트 남음
하나 걸러 반납      : 빈 공간 합계 20520바이트, 가장 큰 덩어리 4104바이트 → 8KB 요청 실패
나머지도 반납       : 이웃한 빈 블록이 합쳐져 가장 큰 덩어리 41040바이트 → 8KB 요청 성공
힙에서 줄어든 양    : xTaskCreate 2152바이트 (스택 2KB + TCB), xTaskCreateStatic 0바이트
힙 기록            : 가장 적게 남았던 때 0바이트, 받기 20번 · 반납 18번, 실패 1번
```

```bash
cmake --preset qemu-mps2 && cmake --build --preset qemu-mps2 --target stack-heap-freertos
./scripts/run_qemu.sh build/qemu-mps2/chapters/stack-heap/stack-heap-freertos.elf
```

## Linux

```text
스택 크기          : 메인 스레드 한도 8192KB (ulimit -s), 새 스레드 기본 8192KB
스택은 아래로      : 한 단계 부를 때마다 지역 변수 주소가 112 · 112 · 112바이트씩 줄어듦
malloc(64MB)       : 받기만 했을 때 +0MB, 다 쓰고 나서 +64MB, free 뒤 +0MB (RSS 기준)
```

macOS는 새 스레드 기본 스택이 512KB, malloc(64MB) 줄은 Linux 전용(`/proc/self/statm`)

## 포인트

- 스택 여유는 `uxTaskGetStackHighWaterMark()`(워드 단위)로 확인. 지역 배열을 2KB로 늘리면 스택 2KB를 넘어서 `[스택 넘침] 태스크 heavy`로 멈춤 (23장)
- 빈 공간 합계가 충분해도 한 덩어리가 작으면 할당 실패. heap_4는 이웃한 빈 블록을 합쳐 줌 (heap_2는 안 합침)
- 실패는 `vApplicationMallocFailedHook()`으로 알 수 있음 (`configUSE_MALLOC_FAILED_HOOK`)
- 정적 할당(`xTaskCreateStatic`)은 메모리를 컴파일할 때 정해서, 힙 부족 · 단편화 걱정이 없음 (`configSUPPORT_STATIC_ALLOCATION`)
- Linux의 큰 malloc은 mmap으로 받고, 실제 메모리는 처음 쓸 때 받음. 그래서 받기만 하면 RSS가 안 늘어남

## 지원 플랫폼

| posix | qemu-mps2 | stm32f4 | arduino-uno |
|---|---|---|---|
| Linux 예제 | ✓ | 예정 | 예정 |
