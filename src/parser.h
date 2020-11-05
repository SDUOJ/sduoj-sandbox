#ifndef SANDBOX_PARSER_H_
#define SANDBOX_PARSER_H_

#include "argtable/argtable3.h"
#include "public.h"

#define INT_PLACEHOLDER "<n>"
#define STR_PLACEHOLDER "<str>"

#define NUM_ALLOWED_ARG 17

struct arg_lit *help, *version;

struct arg_int 
    *max_cpu_time,            /* maximum cpu time(ms) */
    *max_real_time,           /* maximum real time, include blocked time(ms) */
    *max_process_number,
    *max_output_size,
    *uid, *gid;               /* run sandbox in such uid and gid */

struct arg_str 
    *max_memory,              /* maximum virtual memory(byte) */
    *max_stack,               /* maximum stack size(byte), default 16384K */
    *exe_path,                /* executable file that sandbox will run */
    *input_path,              /* executable file will read in */
    *output_path,             /* executable file will print out */
    *log_path,                /* sandbox will print log */
    *exe_args,                /* args and envs for executable file */
    *exe_envs,
    *seccomp_rules;           /* additional seccomp_rules */

struct arg_end *end;

void *arg_table[NUM_ALLOWED_ARG + 1];

/* parse from argv */
void Initialize(int argc, char **argv, struct config *_config);

/* Initialize config from args */
void InitConfig(struct config *_config);
#endif //SANDBOX_PARSER_H_
