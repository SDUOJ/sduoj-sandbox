#ifndef JUDGE_STAT_H_
#define JEDGE_STAT_H_

#include <sys/types.h>

#define VERSION 0x000001
#define PROJECT_NAME "judger"

#define MAX_ARG 256
#define MAX_ENV 256

enum {
    SUCCESS,
    INVALID_CONFIG,
    FORK_ERROR,
    PTHREAD_ERROR,
    WAIT_ERROR,
    DUP2_ERROR,
    SETRLIMIT_ERROR,
    SETUID_ERROR,
    SECCOPM_LOAD_ERROR,
    EXECVE_ERROR,
    SPJ_ERROR,
    ROOT_REQUIRED
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
    int max_cpu_time;
    int max_real_time;
    int max_memory;
    int max_stack;

    char *exe_path;
    char *input_path;
    char *output_path;
    char *log_path;

    char *exe_args[MAX_ARG];
    char *exe_envs[MAX_ENV];
    
    char *seccomp_rules;

    uid_t uid;
    gid_t gid;
};

struct result {
    int cpu_time;
    int real_time;
    int memory_time;
    int signal;
    int eixt_code;
    int error;
    int result;
};

#endif //JUDGE_STAT_H_