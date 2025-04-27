#ifndef SANDBOX_PARSER_H_
#define SANDBOX_PARSER_H_

#include "argtable/argtable3.h"
#include "public.h"

#define INT_PLACEHOLDER "<n>"
#define STR_PLACEHOLDER "<str>"

#define NUM_ALLOWED_ARG 17

// 只声明，不定义
extern struct arg_lit *help, *version;

extern struct arg_int 
    *max_cpu_time,
    *max_real_time,
    *max_process_number,
    *max_output_size,
    *uid, *gid;

extern struct arg_str 
    *max_memory,
    *max_stack,
    *exe_path,
    *input_path,
    *output_path,
    *log_path,
    *exe_args,
    *exe_envs,
    *seccomp_rules;

extern struct arg_end *end;

extern void *arg_table[NUM_ALLOWED_ARG + 1];

/* parse from argv */
void Initialize(int argc, char **argv, struct config *_config);

/* Initialize config from args */
void InitConfig(struct config *_config);

#endif // SANDBOX_PARSER_H_
