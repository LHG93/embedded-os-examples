# 코드 리뷰 체크리스트 (연습 파일)

`review_me.c`는 일부러 문제를 넣은 코드. 빌드 목록(CMakeLists)에 넣지 않았고 실행하지 않음

## 컴파일러 검사

```bash
gcc -Wall -Wextra -fanalyzer -c chapters/review/review_me.c -o /dev/null
```

```text
review_me.c:43:21: warning: leak of ‘buf’ [CWE-401] [-Wanalyzer-malloc-leak]
review_me.c:44:9: warning: use of possibly-NULL ‘buf’ where non-null expected [CWE-690] [-Wanalyzer-possible-null-argument]
```

gcc 15.2(도커 Ubuntu 26.04)에서 확인

## 도구가 못 잡은 것 (리뷰에서 찾을 것)

- `count`: 수신 처리와 로거가 보호 없이 같이 씀 (레이스, 10장)
- `while (!ready) { }`: 바쁜 대기 (굶주림, 7장)
- 로거는 A → B, 송신은 B → A 순서로 잠금 (데드락, 13장)

레이스 · 잠금 순서는 실행 중 검사(ThreadSanitizer, 28장)로 잡을 수 있음. 바쁜 대기는 사람이 봐야 함
