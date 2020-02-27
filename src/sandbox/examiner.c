#define _DEFAULT_SOURCE
#define _POSIX_SOURCE
#define _GNU_SOURCE

#include <grp.h>
#include <sched.h>
#include <errno.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/wait.h>
#include <sys/time.h>
#include <sys/mount.h>

#include "log.h"
#include "examiner.h"
#include "../rules/seccomp_rules.h"

/* Initialize result to zero */
void InitResult(struct result *_result)
{
    _result->cpu_time = 0;
    _result->real_time = 0;
    _result->memory = 0;
    _result->signal = 0;
    _result->exit_code = 0;
    _result->error = 0;
    _result->result = 0;
}

/* Check arguments */
void CheckArgs(FILE *log_fp, struct config *_config, struct result *_result)
{
	// current user must be root
	if (getuid() != 0) ERROR_EXIT(ROOT_REQUIRED);

	// check config
    if (_config->max_cpu_time < 1 || _config->max_real_time < 1 || _config->max_stack < 1 || 
        _config->max_memory < 1 || _config->max_process_number < 1 || _config->max_output_size < 1)
    	ERROR_EXIT(INVALID_CONFIG);
}

void *KillTimeout(void *timeout_info)
{
    pid_t pid = ((struct timeout_info *)timeout_info)->pid;
    int timeout = ((struct timeout_info *)timeout_info)->timeout;
    
    // pthread_detach(pthread_self()) set the thread's status to be unjoinable to release resources; if success, return 0
    if (pthread_detach(pthread_self()) != 0 || sleep((unsigned int)((timeout + 1000) / 1000)) != 0)
    {
        KillProcess(pid);
        return NULL;
    }

    // check in the end
    if (KillProcess(pid) != 0) return NULL;
    return NULL;
}

void ChildProcess(FILE *log_fp, struct config *_config) {
    FILE *input_file = NULL, *output_file = NULL, *error_file = NULL;

    // Check stack
    if (LIMITED(_config->max_stack))
    {
        struct rlimit max_stack;
        max_stack.rlim_cur = max_stack.rlim_max = (rlim_t) (_config->max_stack);
        if (setrlimit(RLIMIT_STACK, &max_stack) != 0)
            CHILD_ERROR_EXIT(SETRLIMIT_FAILED);
    }

    // set memory limit
    if (LIMITED(_config->max_memory))
    {
        struct rlimit max_memory;
        max_memory.rlim_cur = max_memory.rlim_max = (rlim_t) (_config->max_memory) * 2;
        if (setrlimit(RLIMIT_AS, &max_memory) != 0)
            CHILD_ERROR_EXIT(SETRLIMIT_FAILED);
    }

    // set cpu time limit (in seconds)
    if (LIMITED(_config->max_cpu_time))
    {
        struct rlimit max_cpu_time;
        max_cpu_time.rlim_cur = max_cpu_time.rlim_max = (rlim_t) ((_config->max_cpu_time + 1000) / 1000);
        if (setrlimit(RLIMIT_CPU, &max_cpu_time) != 0)
            CHILD_ERROR_EXIT(SETRLIMIT_FAILED);
    }

    // set max process number limit
    if (LIMITED(_config->max_process_number))
    {
        struct rlimit max_process_number;
        max_process_number.rlim_cur = max_process_number.rlim_max = (rlim_t) _config->max_process_number;
        if (setrlimit(RLIMIT_NPROC, &max_process_number) != 0)
            CHILD_ERROR_EXIT(SETRLIMIT_FAILED);
    }

    // set max output size limit
    if (LIMITED(_config->max_output_size))
    {
        struct rlimit max_output_size;
        max_output_size.rlim_cur = max_output_size.rlim_max = (rlim_t ) _config->max_output_size;
        if (setrlimit(RLIMIT_FSIZE, &max_output_size) != 0)
            CHILD_ERROR_EXIT(SETRLIMIT_FAILED);
    }

    if (_config->input_path != NULL)
    {
        input_file = fopen(_config->input_path, "r");
        if (input_file == NULL)
            CHILD_ERROR_EXIT(DUP2_FAILED);
        // redirect file -> stdin
        // On success, these system calls return the new descriptor.
        // On error, -1 is returned, and errno is set appropriately.
        if (dup2(fileno(input_file), fileno(stdin)) == -1)
        {
            // todo log
            CHILD_ERROR_EXIT(DUP2_FAILED);
        }
    }

    if (_config->output_path != NULL)
    {
        output_file = fopen(_config->output_path, "w");
        if (output_file == NULL)
            CHILD_ERROR_EXIT(DUP2_FAILED);
        // redirect stdout -> file
        if (dup2(fileno(output_file), fileno(stdout)) == -1)
            CHILD_ERROR_EXIT(DUP2_FAILED);
    }

    // set gid
    gid_t group_list[] = {_config->gid};
    if (_config->gid != -1 && (setgid(_config->gid) == -1 || setgroups(sizeof(group_list) / sizeof(gid_t), group_list) == -1))
        CHILD_ERROR_EXIT(SETUID_FAILED);

    // set uid
    if (_config->uid != -1 && setuid(_config->uid) == -1)
        CHILD_ERROR_EXIT(SETUID_FAILED);

    // load seccomp
    if (_config->seccomp_rules != NULL)
    {
        if (strcmp("c_cpp", _config->seccomp_rules) == 0 && c_cpp_seccomp_rules(_config) != SUCCESS)
        {
            CHILD_ERROR_EXIT(LOAD_SECCOMP_FAILED);
        }
        else if (strcmp("c_cpp_file_io", _config->seccomp_rules) == 0 && c_cpp_file_io_seccomp_rules(_config) != SUCCESS)
        {
            CHILD_ERROR_EXIT(LOAD_SECCOMP_FAILED);
        }
        else if (strcmp("general", _config->seccomp_rules) == 0 && general_seccomp_rules(_config) != SUCCESS)
        {
            CHILD_ERROR_EXIT(LOAD_SECCOMP_FAILED);
        }
        else if(strcmp("c_cpp", _config->seccomp_rules) != 0 && strcmp("c_cpp_file_io", _config->seccomp_rules) != 0 && strcmp("general", _config->seccomp_rules) != 0)
        {
            // rule does not exist
            CHILD_ERROR_EXIT(LOAD_SECCOMP_FAILED);
        }
    }

    execve(_config->exe_path, _config->exe_args, _config->exe_envs);
    CHILD_ERROR_EXIT(EXECVE_FAILED);
}

/* Monitor process and require status and resource usage */
void RequireUsage(FILE *log_fp, pid_t child_pid, struct config *_config, struct result *_result, struct rusage *resource_usage, int *status)
{
	// create new thread to monitor process running time
    pthread_t tid = 0;
    if (LIMITED(_config->max_real_time))
    {
        struct timeout_info timeout_args = {child_pid, _config->max_real_time};

        if (pthread_create(&tid, NULL, KillTimeout, (void *) (&timeout_args)) != 0)
        {
            KillProcess(child_pid);
            ERROR_EXIT(PTHREAD_FAILED);
        }
    }

    // wait for child process to terminate and require the status and resource usage of the child
    // if success, return the child process ID, else return -1
    if (wait4(child_pid, status, WSTOPPED, resource_usage) == -1)
    {
        KillProcess(child_pid);
        ERROR_EXIT(WAIT_FAILED);
    }

    // process exited, we may need to cancel KillTimeout thread
    if (LIMITED(_config->max_real_time))
    {
        if (pthread_cancel(tid) != 0)
        {
            // todo logging
        };
    }
}

/* Generate the result */
void GenerateResult(FILE *log_fp, struct config *_config, struct result *_result, struct rusage *resource_usage, int *status, struct timeval *start, struct timeval *end)
{
	// get end time
    gettimeofday(end, NULL);
    _result->real_time = (int) (end->tv_sec * 1000 + end->tv_usec / 1000 - start->tv_sec * 1000 - start->tv_usec / 1000);

    // if the child process terminated because it received a signal that was not handled, acquire the signal code
    if (WIFSIGNALED(*status) != 0)
    	_result->signal = WTERMSIG(*status);

    if(_result->signal == SIGUSR1)
        _result->result = SYSTEM_ERROR;
    else
    {
        _result->exit_code = WEXITSTATUS(*status);
        _result->cpu_time = (int) (resource_usage->ru_utime.tv_sec * 1000 +
                                   resource_usage->ru_utime.tv_usec / 1000);
        _result->memory = resource_usage->ru_maxrss * 1024;    /* bytes here, ru_maxrss is in kilobytes */

        if (_result->exit_code != 0)
            _result->result = RUNTIME_ERROR;

        if (_result->signal == SIGSEGV)
        {
            if (LIMITED(_config->max_memory) && _result->memory > _config->max_memory)
                _result->result = MEMORY_LIMIT_EXCEEDED;
            else
                _result->result = RUNTIME_ERROR;
        }
        else
        {
            if (_result->signal != 0)
                _result->result = RUNTIME_ERROR;

            if (LIMITED(_config->max_memory) && _result->memory > _config->max_memory)
                _result->result = MEMORY_LIMIT_EXCEEDED;
            
            if (LIMITED(_config->max_real_time) && _result->real_time > _config->max_real_time)
                _result->result = REAL_TIME_LIMIT_EXCEEDED;
            
            if (LIMITED(_config->max_cpu_time) && _result->cpu_time > _config->max_cpu_time)
                _result->result = CPU_TIME_LIMIT_EXCEEDED;
        }
    }

    LogClose(log_fp);
}

/* Examine and run the code */
void Examine(struct config *_config, struct result *_result)
{
	// initialize log
	FILE *log_fp = LogOpen(_config->log_path);

	CheckArgs(log_fp, _config, _result);
	InitResult(_result);

	struct timeval start, end;
	gettimeofday(&start, NULL);
    
    pid_t child_pid = fork();
    
    // examine the process
    if (child_pid < 0)
    {
    	ERROR_EXIT(FORK_FAILED);
    }
    else if (child_pid == 0)
    {
    	ChildProcess(log_fp, _config);
    }
    else
    {
    	int status;
    	struct rusage resource_usage;
	
		RequireUsage(log_fp, child_pid, _config, _result, &resource_usage, &status);
		GenerateResult(log_fp, _config, _result, &resource_usage, &status, &start, &end);
    }
}