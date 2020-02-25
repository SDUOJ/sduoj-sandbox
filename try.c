#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>

#define TRUE 1
#define FALSE 0

void signal_handler(int sig) {
    // if(sig == SIGALRM) {
    //     printf("Caught SIGALRM!\n");
    //     exit(0);
    // }
    printf("signal: %d\n", sig);
    exit(0);
}


// 先对it_value倒计时，当it_value为零时触发信号，然后重置为it_interval，继续对it_value倒计时，一直这样循环下去。
int set_timer(int sec, int ms, int CPUTime) {
    struct itimerval setter;
    // it_iterval 置 0 只延时, 不定时, 只触发一次信号
    setter.it_interval.tv_sec = setter.it_interval.tv_usec = 0;
    setter.it_value.tv_sec = sec;
    setter.it_value.tv_usec = ms * 1000;

    // ITIMER_REAL：以系统真实的时间来计算，它送出SIGALRM信号。
    // ITIMER_VIRTUAL：以该进程在用户态下花费的时间来计算，它送出SIGVTALRM信号。
    // ITIMER_PROF：以该进程在用户态下和内核态下所费的时间来计算，它送出SIGPROF信号。
    if(setitimer(CPUTime ? ITIMER_VIRTUAL : ITIMER_REAL, &setter, NULL)) {
        perror("setitimer error: ");
        return FALSE;
    }
    return TRUE;
}

int set_resource_limit(int source, unsigned long limit) {
    struct rlimit setter;
    setter.rlim_cur = setter.rlim_max = (rlim_t)limit;
    if(setrlimit(source, &setter) != 0) {
        perror("setrlimit error: ");
        return FALSE;
    }
    return TRUE;
}

int main(int argc, char **argv) {
    signal(SIGALRM, signal_handler);
    signal(SIGXCPU, signal_handler);
    signal(SIGKILL, signal_handler);
    set_timer(1, 500, 0);
    set_resource_limit(RLIMIT_AS, 1024*1024*5);
    set_resource_limit(RLIMIT_CPU, 2);
    for(;;) {
        char *ptr = (char *)calloc(1, 1);
        // if(ptr) printf("success\n");
        // else exit(1);
    }
    return 0;
}