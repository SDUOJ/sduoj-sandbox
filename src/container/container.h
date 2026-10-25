#ifndef SANDBOX_EXAMINER_H_
#define SANDBOX_EXAMINER_H_

#include "../public.h"

#define LIMITED(agrs) (agrs != RLIM_INFINITY)

#define CLOSE_FILE(fp)  \
    {                   \
        if (fp != NULL) \
            fclose(fp); \
    }

#define LOG_ERROR(error_code) LOG_FATAL(log_fp, #error_code)

#define KillProcess(pid) kill(pid, SIGKILL)
#define KillProcessGroup(pid) kill(-(pid), SIGKILL)

#define ERROR_EXIT(error_code)       \
    {                                \
        LOG_ERROR(error_code);       \
        _result->error = error_code; \
        _result->result = SYSTEM_ERROR; \
        LogClose(log_fp);            \
        return;                      \
    }

void Run(struct config *, struct result *);
#endif //SANDBOX_EXAMINER_H_
