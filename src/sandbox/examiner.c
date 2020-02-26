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

#include "tool.h"
#include "examiner.h"
#include "../stat.h"
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

/* Examine and run the code */
void examine(struct config *_config, struct result *_result)
{
	// initialize log fp and result
	FILE *log_fp = LogOpen(_config->log_path);
	InitResult(_result);

	// current user must be root
	if (getuid() != 0) ERROR_EXIT(ROOT_REQUIRED);

	// check args
    if (_config->max_cpu_time < 1 || _config->max_real_time < 1 || _config->max_stack < 1 || 
        _config->max_memory < 1 || _config->max_process_number < 1 || _config->max_output_size < 1)
    	ERROR_EXIT(INVALID_CONFIG);

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
        // create new thread to monitor process running time
        pthread_t tid = 0;
        if (_config->max_real_time != UNLIMITED) {
            struct timeout_killer_args killer_args;

            killer_args.timeout = _config->max_real_time;
            killer_args.pid = child_pid;
            if (pthread_create(&tid, NULL, timeout_killer, (void *) (&killer_args)) != 0) {
                kill_pid(child_pid);
                ERROR_EXIT(PTHREAD_FAILED);
            }
        }

        int status;
        struct rusage resource_usage;

        // wait for child process to terminate
        // on success, returns the process ID of the child whose state has changed;
        // On error, -1 is returned.
        if (wait4(child_pid, &status, WSTOPPED, &resource_usage) == -1) {
            kill_pid(child_pid);
            ERROR_EXIT(WAIT_FAILED);
        }
        // get end time
        gettimeofday(&end, NULL);
        _result->real_time = (int) (end.tv_sec * 1000 + end.tv_usec / 1000 - start.tv_sec * 1000 - start.tv_usec / 1000);

        // process exited, we may need to cancel timeout killer thread
        if (_config->max_real_time != UNLIMITED) {
            if (pthread_cancel(tid) != 0) {
                // todo logging
            };
        }

        if (WIFSIGNALED(status) != 0) {
            _result->signal = WTERMSIG(status);
        }

        if(_result->signal == SIGUSR1) {
            _result->result = SYSTEM_ERROR;
        }
        else {
            _result->exit_code = WEXITSTATUS(status);
            _result->cpu_time = (int) (resource_usage.ru_utime.tv_sec * 1000 +
                                       resource_usage.ru_utime.tv_usec / 1000);
            _result->memory = resource_usage.ru_maxrss * 1024;

            if (_result->exit_code != 0) {
                _result->result = RUNTIME_ERROR;
            }

            if (_result->signal == SIGSEGV) {
                if (_config->max_memory != UNLIMITED && _result->memory > _config->max_memory) {
                    _result->result = MEMORY_LIMIT_EXCEEDED;
                }
                else {
                    _result->result = RUNTIME_ERROR;
                }
            }
            else {
                if (_result->signal != 0) {
                    _result->result = RUNTIME_ERROR;
                }
                if (_config->max_memory != UNLIMITED && _result->memory > _config->max_memory) {
                    _result->result = MEMORY_LIMIT_EXCEEDED;
                }
                if (_config->max_real_time != UNLIMITED && _result->real_time > _config->max_real_time) {
                    _result->result = REAL_TIME_LIMIT_EXCEEDED;
                }
                if (_config->max_cpu_time != UNLIMITED && _result->cpu_time > _config->max_cpu_time) {
                    _result->result = CPU_TIME_LIMIT_EXCEEDED;
                }
            }
        }

	    log_close(log_fp);
	}
}












