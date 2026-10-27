/*
 * syscalls.c — newlib이 찾는 최소한의 시스템 호출
 * printf 같은 표준 출력은 UART0으로, 파일은 없음
 */
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>

#include "mps2.h"

extern char end;                        /* 링커 스크립트: .bss 끝 = 힙 시작 */

void *_sbrk(int incr)
{
    static char *brk = &end;
    char *prev = brk;
    brk += incr;
    return prev;
}

int _write(int fd, const char *buf, int len)
{
    (void)fd;
    for (int i = 0; i < len; i++) {
        while (REG(0x40004004) & 1u) { }
        REG(0x40004000) = (uint32_t)(unsigned char)buf[i];
    }
    return len;
}

int _read(int fd, char *buf, int len) { (void)fd; (void)buf; (void)len; errno = ENOSYS; return -1; }
int _close(int fd) { (void)fd; return -1; }
int _lseek(int fd, int off, int dir) { (void)fd; (void)off; (void)dir; return 0; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
void _exit(int code) { (void)code; for (;;) { } }
