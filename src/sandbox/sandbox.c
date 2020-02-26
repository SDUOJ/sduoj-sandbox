#define _GNU_SOURCE
#define _POSIX_SOURCE

#include "sandbox.h"

#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>

#include "../util.h"

void Start(struct config *_config, struct result *_result)
{
    int status;
    uid_t uid;
    pid_t child_pid;
    struct timeval start, end;
    struct rusage resource_usage;
    
    InitResult(_result);

    if((uid = getuid()) != 0) Halt(ROOT_REQUIRED);

    gettimeofday(&start, NULL);

    child_pid = fork();
    if(child_pid < 0) {
        Halt(FORK_FAILED);
    } else if(child_pid == 0) {
        Judge(_config);
        return;
    }

    if(wait4(child_pid, &status, 2, &resource_usage) == -1) {
        Kill(child_pid);
        Halt(WAIT_FAILED);
    }

    gettimeofday(&end, NULL);
    _result->real_time = (int)((end.tv_sec * 1000 + end.tv_usec / 1000) - (start.tv_sec * 1000 + start.tv_usec / 1000));

    if(WIFEXITED(status) != 0)
        _result->signal = WTERMSIG(status);
    


}

void Judge(struct config *_config) {}

void Kill(pid_t pid) {
    kill(pid, SIGKILL);
}