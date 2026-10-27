# 우선순위 역전

높은 태스크(H)가 낮은 태스크(L)의 잠금을 기다리는 사이, 잠금과 상관없는 중간 태스크(M)가 L을 밀어내서 H가 M까지 기다리게 되는 현상

L과 H가 같은 잠금을 씀. M은 잠금 없이 CPU만 100ms 씀.

| 시각 | 일 |
|---|---|
| 0ms | L이 잠금을 잡고 20ms 분량 일 시작 |
| 2ms | H가 깨어나 잠금을 기다림 |
| 5ms | M이 깨어나 CPU 100ms 사용 |

| 잠금 | H가 기다린 시간 | 그동안 L의 우선순위 |
|---|---|---|
| FreeRTOS 바이너리 세마포어 (상속 없음) | 118ms | 2 (원래 값) |
| FreeRTOS 뮤텍스 (상속) | 18ms | 4 (H 값을 물려받음) |
| Linux `PTHREAD_PRIO_NONE` | 118ms | 10 |
| Linux `PTHREAD_PRIO_INHERIT` | 18ms | 30 (H 값을 물려받음) |
| Linux `PTHREAD_PRIO_PROTECT` (천장 30) | 18ms | 30 (잡는 순간 천장) |

## 실행

```bash
cmake --preset posix && cmake --build --preset posix
./build/posix/chapters/priority-inversion/priority-inversion-freertos
./build/posix/chapters/priority-inversion/priority-inversion-linux
```

## Linux 예제 조건

- SCHED_FIFO 실시간 우선순위 + CPU 0 하나에 고정. Linux 전용 (macOS에서는 안내만 출력)
- 권한 필요: `sudo`, 또는 rtprio 제한. `compose.yaml`에 `ulimits: rtprio: 99` 설정됨
- 권한이 없으면 "건너뜀"을 출력하고 정상 종료(0)
- 도커 데스크톱(맥 · 윈도우) 일부 버전은 컨테이너의 실시간 스케줄링 자체를 막음(RT 그룹 스케줄링). 이때도 "건너뜀"
- 위 결과는 도커 데스크톱 VM(Linux 5.15)에서 privileged 컨테이너를 루트 cgroup으로 옮겨서 확인함

  ```bash
  docker run --rm -it --privileged --user root --cgroupns=host -v "$PWD":/work -w /work \
      embedded-os-examples:26.04 bash -c 'echo $$ > /sys/fs/cgroup/cgroup.procs && exec bash'
  ```

## 포인트

- 역전은 "H가 L을 기다림" 자체가 아니라, 잠금과 상관없는 M 때문에 기다림이 끝없이 길어지는 것
- 우선순위 상속: H가 기다리는 동안만 L을 H 값으로 올림. FreeRTOS 뮤텍스는 자동, 바이너리 세마포어는 없음
- 우선순위 천장: 잠금마다 "쓰는 태스크 중 가장 높은 값"을 정해 두고, 잡는 순간 그 값으로 올림. AUTOSAR OS 리소스가 이 방식
- Linux 예제 메인 스레드는 출발 직후까지 CPU를 깨워 둠. CPU가 쉬면 깨어나는 데 시간이 걸려(VM은 수 ms) 출발이 늦어짐
- 오래 걸리는 일을 잠근 채로 하지 않는 것이 먼저. 상속은 역전을 줄일 뿐 L의 일 시간(18ms)은 그대로 기다림

## 지원 플랫폼

| posix | qemu-mps2 | stm32f4 | arduino-uno |
|---|---|---|---|
| ✓ | 예정 | 예정 | 예정 |
