#ifndef SANDBOX_EXAMINER_H_
#define SANDBOX_EXAMINER_H_

#include "../public.h"

#define LIMITED(agrs) (agrs != RLIM_INFINITY)

#define CLOSE_FILE(fp)  \
    {                   \
        if (fp != NULL) \
            fclose(fp); \
    }

#define LOG_ERROR(error_code) LOG_FATAL(log_fp, "Error: " #error_code)

#define KillProcess(pid) kill(pid, SIGKILL)

#define ERROR_EXIT(error_code)       \
    {                                \
        LOG_ERROR(error_code);       \
        _result->error = error_code; \
        LogClose(log_fp);            \
        return;                      \
    }

#define CHILD_ERROR_EXIT(error_code)                                                                 \
    {                                                                                                \
        LOG_FATAL(log_fp, "Error: System errno: %s; Internal errno: " #error_code, strerror(errno)); \
        CLOSE_FILE(input_file);                                                                      \
        if (output_file == error_file)                                                               \
        {                                                                                            \
            CLOSE_FILE(output_file);                                                                 \
        }                                                                                            \
        else                                                                                         \
        {                                                                                            \
            CLOSE_FILE(output_file);                                                                 \
            CLOSE_FILE(error_file);                                                                  \
        }                                                                                            \
        raise(SIGUSR1);                                                                              \
        exit(EXIT_FAILURE);                                                                          \
    }

struct timeout_info
{
    pid_t pid;
    int timeout;
};

void Examine(struct config *, struct result *);
#endif //SANDBOX_EXAMINER_H_