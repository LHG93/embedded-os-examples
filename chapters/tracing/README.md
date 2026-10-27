# 트레이싱

"누가 언제 CPU를 받았나"를 시간과 함께 기록해 다시 돌려 봄. 로그(printf)는 타이밍을 바꾸지만, 트레이스는 짧은 기록만 남기고 나중에 한꺼번에 봄

## FreeRTOS (qemu-mps2 전용)

트레이스 훅 `traceTASK_SWITCHED_IN`(qemu-mps2 `FreeRTOSConfig.h` → board.c의 빈 `board_trace_switch_in`)을 예제가 덮어써서 25MHz 시계와 함께 기록. 타이머 ISR은 직접 표시를 남김

```text
1. 기록 (처음 14개, 시간은 기록 시작부터, 전체 30개)
        0.033ms  → sensor
        0.392ms  → IDLE
        0.774ms  → logger
        3.781ms  → IDLE
        5.000ms  ! TIMER0
        5.033ms  → handler
        5.242ms  → sensor
        5.547ms  → logger
        8.553ms  → IDLE
       10.124ms  → sensor
       10.429ms  → logger
       12.034ms  ! TIMER0
       12.038ms  → handler
       12.246ms  → logger
2. 타임라인 (0.5ms 한 칸 · 30ms, # = CPU 사용, | = 타이머 ISR)
   handler  ..........#.............#............##.............#.......
   sensor   #.........##........#.........#.........#........##.........
   logger   .#######...#######..#######...#######...#######...#######...
   IDLE     ##.....####......####.....#####.....#####.....####......####
   ISR      ..........|.............|............|..............|.......
3. CPU 사용 (30ms): handler 2.7% · sensor 6.2% · logger 58.7% · IDLE 32.1%
   CPU를 받은 횟수: handler 4 · sensor 6 · logger 8, 타이머 ISR 4번, ISR → handler 최대 32us
```

시각은 실행마다 몇 us씩 다름

## Linux (ftrace)

커널의 `sched_switch` 이벤트 + `trace_marker`. tracefs에 쓸 권한 필요(root · 권한 준 컨테이너). 없으면 "건너뜀"

```text
ftrace 기록 (ping · pong이 낀 sched_switch + trace_marker, CPU 0)
        0.0us  표시: ping: 보냄 1
        4.0us  ping   → tracing-linux (ping 잠듦)
       12.0us  표시: pong: 받고 돌려줌
       17.0us  pong   → ping         (pong 잠듦)
       18.0us  표시: ping: 보냄 2
       22.0us  ping   → pong         (ping 잠듦)
       23.0us  표시: pong: 받고 돌려줌
       26.0us  pong   → ping         (pong 잠듦)
       28.0us  표시: ping: 보냄 3
       31.0us  ping   → pong         (ping 잠듦)
       32.0us  표시: pong: 받고 돌려줌
      104.0us  pong   → ping         (pong 밀려남)
      110.0us  ping   → tracing-linux (ping 밀려남)
전환 7번 (ping ↔ pong 바로 넘김 5번), 표시 6개
```

- 첫 줄의 `tracing-linux`는 이름을 바꾸기 전의 pong, 마지막 줄은 join에서 깨어난 메인 스레드
- 예제가 끝나면 tracefs 설정(이벤트 · 거르개)을 원래대로 돌려놓음

```bash
docker run --rm -it --privileged --user root -v "$PWD":/work -w /work embedded-os-examples:26.04 bash
```

## 실행

```bash
# FreeRTOS: QEMU
cmake --preset qemu-mps2 && cmake --build --preset qemu-mps2
./scripts/run_qemu.sh build/qemu-mps2/chapters/tracing/tracing-freertos.elf

# Linux: 권한 준 컨테이너 안에서
cmake --preset posix && cmake --build --preset posix
./build/posix/chapters/tracing/tracing-linux
```

## 바꿔 보기

| 바꿀 곳 | 결과 |
|---|---|
| FreeRTOS `TRACE_JSON` 0 → 1 | 끝에 `---- trace.json 시작 ----` ~ `끝` 사이에 Chrome trace 형식 JSON이 나옴. 그 줄만 파일로 저장해 ui.perfetto.dev에 올리면 태스크별 막대 그림으로 봄 (JSON 30개 항목 파싱 확인) |

## 포인트

- 트레이스 훅은 짧게: 시각 · 누구만 배열에 적고, 출력은 끝난 뒤. 훅 안에서 printf하면 그 자체가 타이밍을 바꿈
- ISR · 사용자 표시를 같은 기록에 넣으면 "ISR → 처리 태스크까지 39us" 같은 숫자가 바로 나옴(19장)
- SEGGER SystemView · Percepio Tracealyzer는 같은 일을 디버거 · 전용 버퍼로 하고 그림까지 그려 줌
- Linux는 ftrace(tracefs) · trace-cmd · perf. 거르개(filter)로 필요한 이벤트만 남김

## 지원 플랫폼

| posix | qemu-mps2 | stm32f4 | arduino-uno |
|---|---|---|---|
| Linux 예제만 | ✓ | 예정 | 예정 |
