/*
 * board.h — 장 예제와 보드 사이의 약속
 *
 * 장 예제 FreeRTOS 코드는 이 파일의 함수만 사용.
 * 구현은 보드마다 platforms/<이름>/board.c.
 * 새 보드를 추가해도 장 예제 코드는 그대로.
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>

/* 보드 초기화. main()에서 스케줄러 시작 전에 한 번 호출 */
void board_init(void);

/* 한 줄 출력. printf 형식, 줄바꿈 자동.
 * POSIX는 터미널, 보드는 UART로 출력 */
void board_log(const char *fmt, ...);

/* LED 켜기 · 끄기. POSIX는 "[LED] on/off" 출력 */
void board_led(bool on);

/* 스케줄러 종료 후 main()으로 복귀 (POSIX 전용).
 * 보드는 돌아갈 곳이 없어서 결과 출력 후 정지 */
void board_finish(int exit_code);

/* board_finish()에 넘긴 값. main() 반환값으로 사용 */
int board_exit_code(void);

#endif
