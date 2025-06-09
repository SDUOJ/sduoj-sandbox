#ifndef SANDBOX_PUBLIC_H_
#define SANDBOX_PUBLIC_H_

#include <sys/types.h>
#include <sys/resource.h>
#include <stdint.h>

#define PROJECT_NAME "sandbox"
#define VERSION 0x010000

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
    PTRACE_FAILED,          /* ptrace failed */
    SPJ_ERROR,              /* run Special Judge failed */
    ROOT_REQUIRED,          /* sandbox needs root privilege */
    NOBODY_REQUIRED         /* user program needs run in NOBODY */
};

enum {
    CPU_TIME_LIMIT_EXCEEDED = 1,
    REAL_TIME_LIMIT_EXCEEDED = 2,
    MEMORY_LIMIT_EXCEEDED = 3,
    RUNTIME_ERROR = 4,
    SYSTEM_ERROR = 5,
    OUTPUT_LIMIT_EXCEEDED = 6,
};

struct config {
    rlim_t max_cpu_time;
    rlim_t max_real_time;
    rlim_t max_memory;
    rlim_t max_stack;
    rlim_t max_process_number;
    rlim_t max_output_size;

    char *exe_path;
    char *input_path;
    char *output_path;
    char *log_path;

    char *exe_args[MAX_ARG + 1];
    char *exe_envs[MAX_ENV + 1];
    
    char *seccomp_rules;

    int print_args;

    uid_t uid;
    gid_t gid;
};

struct result {
    uint32_t cpu_time;
    uint32_t real_time;
    uint64_t memory;
    int signal;
    int exit_code;
    int error;
    int result;
};

#endif //SANDBOX_PUBLIC_H_
