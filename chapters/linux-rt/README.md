# Linux 실시간 (PREEMPT_RT)

일반 리눅스를 실시간으로 쓸 때 늦어지는 원인(스케줄링 정책 · 페이지 폴트 · 커널 안 선점 불가 구간)과 대책. 실시간 커널(PREEMPT_RT)이 무엇을 바꾸는지

## Linux

도커 데스크톱 VM(Linux 5.15, 일반 PREEMPT 커널), privileged 컨테이너에서 실행

```text
0. 커널 5.15.49-linuxkit : #1 SMP PREEMPT Tue Sep 13 07:51:32 UTC 2022 → PREEMPT_RT 아님 (일반 커널)
1. 새 메모리 16KB 쓰기 200번 (페이지 4KB × 4)
   처음 쓰는 메모리      : 평균   6.2us, 최대   11.7us, 페이지 폴트 802번
   미리 건드려 둔 메모리 : 평균   0.1us, 최대    0.2us, 페이지 폴트 0번
2. 1ms 주기 루프 1000번, 매번 새 16KB 쓰기, 같은 CPU에 바쁜 스레드 2개 (늦음 = 깨어남 + 일)
   일반 정책 · 대책 없음    : 평균    72us, 최대   3771us, 500us 넘음 4번, 루프 안 폴트 4000번
     분포: <20us 0 · <50 0 · <100 993 · <500 3 · <1ms 1 · 1ms 이상 3
   FIFO + mlockall + 미리   : 평균     9us, 최대     45us, 500us 넘음 0번, 루프 안 폴트 0번
     분포: <20us 755 · <50 245 · <100 0 · <500 0 · <1ms 0 · 1ms 이상 0
```

- 2의 실시간 쪽은 SCHED_FIFO · mlockall 권한 필요. 없으면 "건너뜀" 후 정상 종료
  - `compose.yaml`에 `rtprio: 99` · `memlock: -1` 설정됨. 도커 데스크톱은 그래도 SCHED_FIFO가 막혀 있어 privileged 방법 필요 ([priority-inversion](../priority-inversion/README.md#linux-예제-조건))
- 이 커널은 PREEMPT_RT가 아님. 실시간 커널과의 비교는 실시간 커널을 깐 PC · VM이 필요해서 아직 못 함
- Linux 전용. macOS에서는 안내만 출력

## 실행

```bash
cmake --preset posix && cmake --build --preset posix
./build/posix/chapters/linux-rt/linux-rt-linux
```

## 바꿔 보기

| 바꿀 곳 | 결과 |
|---|---|
| `cyclic()`의 `if (r->rt) touch(...)` 두 줄 지우기 | 실시간 쪽 루프 안 폴트가 그래도 0번. `mlockall(MCL_FUTURE)`가 새로 받는 메모리를 받을 때 바로 채워 줌 |

## 포인트

- 처음 쓰는 메모리는 그때 커널이 페이지를 마련함(폴트). 4KB 페이지 하나에 1us 남짓, 루프 안에서 쌓이면 지연
- 대책: `mlockall(MCL_CURRENT | MCL_FUTURE)` + 루프 전에 버퍼 · 스택을 미리 건드리기. 루프 안에서 malloc · 파일 입출력 안 하기
- 큰 영역은 2MB 페이지(THP)로 받기도 함. 폴트 수는 적지만 한 번이 큼. 예제는 `MADV_NOHUGEPAGE`로 4KB 페이지를 씀
- 일반 커널도 SCHED_FIFO로 지연이 크게 줄지만, 커널 안 선점 불가 구간이 남음. PREEMPT_RT는 스핀락을 잠자는 잠금으로, 인터럽트 처리를 스레드로 바꿔 그 구간을 줄임 (리눅스 6.12부터 기본 커널에 포함)
- 커널 스레드 `migration/N`이 이미 FIFO 99로 돎. 내 스레드는 99보다 낮게(보통 50 ~ 80)

## 지원 플랫폼

| posix | qemu-mps2 | stm32f4 | arduino-uno |
|---|---|---|---|
| Linux 예제만 | – | – | – |
