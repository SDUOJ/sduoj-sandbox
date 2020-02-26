#ifndef JUDGE_STAT_H_
#define JUDGE_STAT_H_

#include <sys/types.h>
#include <sys/resource.h>

#define PROJECT_NAME "judger"
#define VERSION 0x000001

#define MAX_ARG 256
#define MAX_ENV 256

enum {
    SUCCESS,                /* everything is ok */
    INVALID_CONFIG,         /* invalied config */
    FORK_FAILED,            /* run fork() failed */
    PTHREAD_FAILED,         /* run child thread failed */
    WAIT_FAILED,            /* run wait4() failed */
    DUP2_FAILED,            /* run dup2() failed */
    SETRLIMIT_FAILED,       /* run setrlimit() failed */
    SETUID_FAILED,          /* run setuid() failed */
    LOAD_SECCOMP_FAILED,    /* load seccomp rules failed */
    EXECVE_FAILED,          /* run execve() failed */
    SPJ_ERROR,              /* run Special Judge failed */
    ROOT_REQUIRED,          /* judger needs root privilege */
    NOBODY_REQUIRED         /* user program needs run in NOBODY */
};

enum {
    WRONG_ANSWER = -1,
    CPU_TIME_LIMIT_EXCEEDED = -2,
    REAL_TIME_LIMIT_EXCEEDED = -3,
    MEMORY_LIMIT_EXCEEDED = -4,
    RUNTIME_ERROR = -5,
    SYSTEM_ERROR = -6,
};

struct config {
    rlim_t max_cpu_time;
    rlim_t max_real_time;
    rlim_t max_memory;
    rlim_t max_stack;

    char *exe_path;
    char *input_path;
    char *output_path;
    char *log_path;

    char *exe_args[MAX_ARG + 1];
    char *exe_envs[MAX_ENV + 1];
    
    char *seccomp_rules;

    uid_t uid;
    gid_t gid;
};

struct result {
    int cpu_time;
    int real_time;
    int memory;
    int signal;
    int exit_code;
    int error;
    int result;
};

#endif //JUDGE_STAT_H_