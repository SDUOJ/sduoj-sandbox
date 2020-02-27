#include "util.h"

#include <pwd.h>
#include <string.h>
#include <stdlib.h>

#include "parser.h"

void GetNobody(int *uid, int *gid)
{
    struct passwd *nobody;
    nobody = getpwnam("nobody");
    if (!nobody)
    {
        Halt(NOBODY_REQUIRED);
    }
    else
    {
        *uid = nobody->pw_uid;
        *gid = nobody->pw_gid;
    }
}

void Halt(int exit_coide)
{
    arg_freetable(arg_table, sizeof(arg_table) / sizeof(arg_table[0]));
    exit(exit_coide);
}

void UnexceptedArg()
{
    arg_print_errors(stdout, end, PROJECT_NAME);
    printf("Try '%s --help' for more information.\n", PROJECT_NAME);
}

void PrintUsage()
{
    printf("Usage: %s", PROJECT_NAME);
    arg_print_syntax(stdout, arg_table, "\n\n");
    arg_print_glossary(stdout, arg_table, "  %-25s %s\n");
}

void PrintVersion()
{
    printf("Version: %d.%d.%d\n", (VERSION >> 16) & 0xff, (VERSION >> 8) & 0xff, VERSION & 0xff);
}

void InitConfig(struct config *_config)
{
    int i, nobody_uid, nobody_gid;

    _config->max_cpu_time = max_cpu_time->count > 0 ? (rlim_t)*max_cpu_time->ival : RLIM_INFINITY;
    _config->max_real_time = max_real_time->count > 0 ? (rlim_t)*max_real_time->ival : RLIM_INFINITY;
    _config->max_memory = max_memory->count > 0 ? (rlim_t)*max_memory->ival : RLIM_INFINITY;
    _config->max_stack = max_stack->count > 0 ? (rlim_t)*max_stack->ival : 16 * 1024 * 1024;
    _config->max_process_number = max_process_number->count > 0 ? (rlim_t)*max_process_number->ival : RLIM_INFINITY;
    _config->max_output_size = max_output_size->count > 0 ? (rlim_t)*max_output_size->ival : RLIM_INFINITY;

    _config->exe_path = (char *)exe_path->sval[0];
    _config->input_path = input_path->count > 0 ? (char *)input_path->sval[0] : "/dev/stdin";
    _config->output_path = output_path->count > 0 ? (char *)output_path->sval[0] : "/dev/stdout";
    _config->log_path = log_path->count > 0 ? (char *)log_path->sval[0] : "judger.log";

    _config->exe_args[0] = _config->exe_path;
    for (i = 1; i <= exe_args->count; i++)
    {
        _config->exe_args[i] = (char *)exe_args->sval[i - 1];
    }
    _config->exe_args[i] = NULL;

    for (i = 0; i < exe_envs->count; i++)
    {
        _config->exe_envs[i] = (char *)exe_envs->sval[i];
    }
    _config->exe_envs[i] = NULL;

    _config->seccomp_rules = (char *)seccomp_rules->sval[0];

    GetNobody(&nobody_uid, &nobody_gid);
    _config->uid = uid->count > 0 ? (uid_t)*uid->ival : nobody_uid;
    _config->gid = gid->count > 0 ? (gid_t)*gid->ival : nobody_gid;
}