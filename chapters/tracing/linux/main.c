/*
 * 트레이싱 — Linux (ftrace)
 *
 * 커널의 sched_switch 이벤트로 "누가 언제 CPU를 받았나"를 기록. 스레드 ping · pong이 파이프로 3번 주고받음
 * trace_marker에 쓴 글(사용자 표시)도 같은 기록에 끼어 들어감. 거르개(filter)로 ping · pong이 낀 전환만 남김
 * 기록은 커널 안 링 버퍼에 쌓이고, 끝난 뒤 trace 파일로 읽음
 *
 * tracefs(/sys/kernel/tracing)에 쓸 권한 필요 (root · 권한 준 컨테이너). 없으면 건너뜀
 * Linux 전용. 다른 OS에서는 안내만 출력
 */
#define _GNU_SOURCE
#include <stdio.h>

#ifndef __linux__
int main(void)
{
    printf("Linux 전용 예제 (ftrace). 도커 컨테이너 안에서 실행\n");
    return 0;
}
#else

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>

#define ROUNDS 3

static const char *T = "/sys/kernel/tracing";
static int ab[2], ba[2], marker = -1;

static int wr(const char *file, const char *val)
{
    char path[128];
    snprintf(path, sizeof path, "%s/%s", T, file);
    int fd = open(path, O_WRONLY | O_TRUNC);
    if (fd < 0)
        return -1;
    int r = write(fd, val, strlen(val)) == (ssize_t)strlen(val) ? 0 : -1;
    close(fd);
    return r;
}

static void mark(const char *s)
{
    if (marker >= 0 && write(marker, s, strlen(s)) < 0) { }
}

static void *pong(void *arg)
{
    (void)arg;
    char b;
    pthread_setname_np(pthread_self(), "pong");
    for (int i = 0; i < ROUNDS; i++) {
        if (read(ab[0], &b, 1) != 1)
            break;
        mark("pong: 받고 돌려줌");
        if (write(ba[1], &b, 1) != 1)
            break;
    }
    return NULL;
}

static void *ping(void *arg)
{
    (void)arg;
    char b = 'x', msg[32];
    pthread_setname_np(pthread_self(), "ping");
    for (int i = 0; i < ROUNDS; i++) {
        snprintf(msg, sizeof msg, "ping: 보냄 %d", i + 1);
        mark(msg);
        if (write(ab[1], &b, 1) != 1 || read(ba[0], &b, 1) != 1)
            break;
    }
    return NULL;
}

/* 한 줄을 짧게: "시각  ping → pong (ping 잠듦)" 또는 "시각  표시: …" */
static int show(const char *line, double *t0)
{
    const char *colon = strstr(line, ": sched_switch: ");
    const char *mk = strstr(line, ": tracing_mark_write: ");
    if (!colon && !mk)
        return 0;
    const char *ts = colon ? colon : mk;
    const char *p = ts;
    while (p > line && p[-1] != ' ')
        p--;
    double t = atof(p);
    if (*t0 < 0)
        *t0 = t;
    double us = (t - *t0) * 1e6;
    if (mk) {
        char text[96];
        snprintf(text, sizeof text, "%s", mk + strlen(": tracing_mark_write: "));
        text[strcspn(text, "\n")] = '\0';
        printf("   %8.1fus  표시: %s\n", us, text);
        return 1;
    }
    char prev[32] = "", next[32] = "", st[8] = "";
    const char *a = strstr(colon, "prev_comm="), *s = strstr(colon, "prev_state="), *n = strstr(colon, "next_comm=");
    if (!a || !s || !n)
        return 0;
    sscanf(a, "prev_comm=%31s", prev);
    sscanf(s, "prev_state=%7s", st);
    sscanf(n, "next_comm=%31s", next);
    printf("   %8.1fus  %-6s → %-12s (%s %s)\n", us, prev, next, prev,
           st[0] == 'S' ? "잠듦" : st[0] == 'R' ? "밀려남" : st);
    return 2;
}

int main(void)
{
    if (access("/sys/kernel/tracing/trace", F_OK) != 0)
        mount("nodev", T, "tracefs", 0, NULL);      /* 권한 있으면 tracefs 붙이기 */
    if (access("/sys/kernel/tracing/trace_marker", W_OK) != 0) {
        printf("ftrace를 쓸 수 없어 건너뜀 (tracefs 쓰기 권한 없음). root · 권한 준 컨테이너 필요 (README 참고)\n");
        return 0;
    }

    cpu_set_t one;
    CPU_ZERO(&one);
    CPU_SET(0, &one);
    sched_setaffinity(0, sizeof one, &one);         /* 둘 다 CPU 0에서. 전환이 기록에 다 보이게 */

    /* 준비: 버퍼 비우기, ping · pong이 낀 전환만 남기는 거르개, sched_switch 켜기 */
    wr("tracing_on", "0");
    wr("trace", "");
    wr("current_tracer", "nop");
    wr("events/sched/sched_switch/filter",
       "prev_comm == \"ping\" || prev_comm == \"pong\" || next_comm == \"ping\" || next_comm == \"pong\"");
    wr("events/sched/sched_switch/enable", "1");
    marker = open("/sys/kernel/tracing/trace_marker", O_WRONLY);
    if (pipe(ab) || pipe(ba))
        return 1;

    pthread_t tp, tq;
    wr("tracing_on", "1");
    pthread_create(&tq, NULL, pong, NULL);
    pthread_create(&tp, NULL, ping, NULL);
    pthread_join(tp, NULL);
    pthread_join(tq, NULL);
    wr("tracing_on", "0");

    /* 읽기 */
    FILE *f = fopen("/sys/kernel/tracing/trace", "r");
    char line[512];
    double t0 = -1;
    int switches = 0, marks = 0, pp = 0;
    printf("ftrace 기록 (ping · pong이 낀 sched_switch + trace_marker, CPU 0)\n");
    while (f && fgets(line, sizeof line, f)) {
        if (line[0] == '#')
            continue;
        int k = show(line, &t0);
        if (k == 1)
            marks++;
        if (k == 2) {
            switches++;
            pp += (strstr(line, "prev_comm=ping") && strstr(line, "next_comm=pong")) ||
                  (strstr(line, "prev_comm=pong") && strstr(line, "next_comm=ping"));
        }
    }
    if (f)
        fclose(f);
    printf("전환 %d번 (ping ↔ pong 바로 넘김 %d번), 표시 %d개\n", switches, pp, marks);

    /* 정리: 원래대로 */
    if (marker >= 0)
        close(marker);
    wr("events/sched/sched_switch/enable", "0");
    wr("events/sched/sched_switch/filter", "0");
    wr("trace", "");
    wr("tracing_on", "1");
    return marks == ROUNDS * 2 && pp >= ROUNDS ? 0 : 1;
}
#endif
