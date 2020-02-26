#define _POSIX_SOURCE
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/file.h>

#include "tool.h"

/* ------------- Tool of killing process */

int KillProcess(pid_t pid)
{
    return kill(pid, SIGKILL);
}

void *KillTimeout(void *time_limit_exceeded)
{
    // create a new thread to kill the timeout process
    pid_t pid = ((struct time_limit_exceeded *)time_limit_exceeded)->pid;
    int timeout = ((struct time_limit_exceeded *)time_limit_exceeded)->timeout;
    
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

/* ------------- Tool of maintaining log */

FILE *LogOpen(const char *filename)
{
    FILE *log_fp = fopen(filename, "a");
    if (log_fp == NULL) fprintf(stderr, "can not open log file %s", filename);
    return log_fp;
}


void LogClose(FILE *log_fp)
{
    if (log_fp != NULL) fclose(log_fp);
}


void LogWrite(int level, const char *source_filename, const int line, const FILE *log_fp, const char *fmt, ...)
{
    char LOG_LEVEL_NOTE[][10] = {"FATAL", "WARNING", "INFO", "DEBUG"};
    if (log_fp == NULL)
    {
        fprintf(stderr, "can not open log file");
        return;
    }
    static char buffer[LOG_BUFFER_SIZE];
    static char log_buffer[LOG_BUFFER_SIZE];
    static char datetime[100];
    static char line_str[20];
    static time_t now = time(NULL);

    strftime(datetime, 99, "%Y-%m-%d %H:%M:%S", localtime(&now));
    snprintf(line_str, 19, "%d", line);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(log_buffer, LOG_BUFFER_SIZE, fmt, ap);
    va_end(ap);

    int count = snprintf(buffer, LOG_BUFFER_SIZE,
                         "%s [%s] [%s:%s]%s\n",
                         LOG_LEVEL_NOTE[level], datetime, source_filename, line_str, log_buffer);
    int log_fd = fileno((FILE *) log_fp);
    
    if (flock(log_fd, LOCK_EX) == 0)
    {
        if (write(log_fd, buffer, (size_t) count) < 0)
        {
            fprintf(stderr, "write error");
            return;
        }
        flock(log_fd, LOCK_UN);
    }
    else
    {
        fprintf(stderr, "flock error");
        return;
    }
}