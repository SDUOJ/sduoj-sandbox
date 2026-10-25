#include "parser.h"
#include "util.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ERROR 10

struct arg_lit *help, *version;
struct arg_int *max_cpu_time, *max_real_time, *max_process_number, *uid, *gid;
struct arg_int *isolate_fs, *work_readonly;
struct arg_str *max_memory, *max_stack, *max_output_size, *max_io_input_size;
struct arg_str *exe_path, *input_path, *output_path, *error_path, *io_mode;
struct arg_str *cgroup_root;
struct arg_str *log_path, *exe_args, *exe_envs, *seccomp_rules;
struct arg_end *end;

void *arg_table[NUM_ALLOWED_ARG + 1];

static rlim_t ParseLimit(struct arg_str *arg, rlim_t fallback)
{
    char *endptr = NULL;
    unsigned long long value;

    if (arg->count == 0)
        return fallback;
    errno = 0;
    value = strtoull(arg->sval[0], &endptr, 10);
    if (errno != 0 || arg->sval[0][0] == '-' || endptr == arg->sval[0] || *endptr != '\0' ||
        value == 0 || value >= RLIM_INFINITY) {
        UnexceptedArg();
        Halt(INVALID_CONFIG);
    }
    return (rlim_t)value;
}

void Initialize(int argc, char **argv, struct config *_config)
{
    arg_table[0] = (help = arg_litn(NULL, "help", 0, 1, "Display help and exit."));
    arg_table[1] = (version = arg_litn(NULL, "version", 0, 1, "Display version info and exit."));
    arg_table[2] = (max_cpu_time = arg_intn(NULL, "max_cpu_time", INT_PLACEHOLDER, 0, 1, "Max CPU time (ms)."));
    arg_table[3] = (max_real_time = arg_intn(NULL, "max_real_time", INT_PLACEHOLDER, 0, 1, "Max wall time (ms)."));
    arg_table[4] = (max_memory = arg_strn(NULL, "max_memory", STR_PLACEHOLDER, 0, 1, "Max resident memory (bytes)."));
    arg_table[5] = (max_stack = arg_strn(NULL, "max_stack", STR_PLACEHOLDER, 0, 1, "Max stack size (bytes)."));
    arg_table[6] = (max_process_number = arg_intn(NULL, "max_process_number", INT_PLACEHOLDER, 0, 1, "Max process count."));
    arg_table[7] = (max_output_size = arg_strn(NULL, "max_output_size", STR_PLACEHOLDER, 0, 1, "Max output bytes."));
    arg_table[8] = (exe_path = arg_str1(NULL, "exe_path", STR_PLACEHOLDER, "Executable path."));
    arg_table[9] = (input_path = arg_strn(NULL, "input_path", STR_PLACEHOLDER, 0, 1, "Input file copied to stdin memfd."));
    arg_table[10] = (output_path = arg_strn(NULL, "output_path", STR_PLACEHOLDER, 0, 1, "Where stdout is copied after execution."));
    arg_table[11] = (error_path = arg_strn(NULL, "error_path", STR_PLACEHOLDER, 0, 1, "Where stderr is copied after execution."));
    arg_table[12] = (log_path = arg_strn(NULL, "log_path", STR_PLACEHOLDER, 0, 1, "Sandbox log path."));
    arg_table[13] = (exe_args = arg_strn(NULL, "exe_args", STR_PLACEHOLDER, 0, 255, "Executable arguments."));
    arg_table[14] = (exe_envs = arg_strn(NULL, "exe_envs", STR_PLACEHOLDER, 0, 255, "Executable environment."));
    arg_table[15] = (seccomp_rules = arg_strn(NULL, "seccomp_rules", STR_PLACEHOLDER, 0, 1, "Seccomp rules."));
    arg_table[16] = (uid = arg_intn(NULL, "uid", INT_PLACEHOLDER, 0, 1, "UID for executable."));
    arg_table[17] = (gid = arg_intn(NULL, "gid", INT_PLACEHOLDER, 0, 1, "GID for executable."));
    arg_table[18] = (io_mode = arg_strn(NULL, "io_mode", STR_PLACEHOLDER, 0, 1, "I/O backend; only memfd is supported."));
    arg_table[19] = (max_io_input_size = arg_strn(NULL, "max_io_input_size", STR_PLACEHOLDER, 0, 1, "Maximum input bytes copied to memory."));
    arg_table[20] = (isolate_fs = arg_intn(NULL, "isolate_fs", INT_PLACEHOLDER, 0, 1, "Require private mount root and Landlock isolation."));
    arg_table[21] = (work_readonly = arg_intn(NULL, "work_readonly", INT_PLACEHOLDER, 0, 1, "Expose the working directory read-only."));
    arg_table[22] = (cgroup_root = arg_strn(NULL, "cgroup_root", STR_PLACEHOLDER, 0, 1, "Writable cgroup v2 root for this run."));
    arg_table[23] = (end = arg_end(MAX_ERROR));

    int nerrors = arg_parse(argc, argv, arg_table);
    if (help->count > 0) {
        PrintUsage();
        Halt(0);
    }
    if (version->count > 0) {
        PrintVersion();
        Halt(0);
    }
    if (nerrors > 0) {
        UnexceptedArg();
        Halt(INVALID_CONFIG);
    }

    signal(SIGINT, Halt);
    InitConfig(_config);
}

void InitConfig(struct config *_config)
{
    int i, nobody_uid, nobody_gid;

    _config->max_cpu_time = max_cpu_time->count > 0 ? (rlim_t)*max_cpu_time->ival : RLIM_INFINITY;
    _config->max_real_time = max_real_time->count > 0 ? (rlim_t)*max_real_time->ival : RLIM_INFINITY;
    _config->max_memory = ParseLimit(max_memory, RLIM_INFINITY);
    _config->max_stack = ParseLimit(max_stack, (rlim_t)16 * 1024 * 1024);
    _config->max_process_number = max_process_number->count > 0 ? (rlim_t)*max_process_number->ival : 128;
    _config->max_output_size = ParseLimit(max_output_size, RLIM_INFINITY);
    _config->max_io_input_size = ParseLimit(max_io_input_size, (rlim_t)64 * 1024 * 1024);

    _config->exe_path = (char *)exe_path->sval[0];
    _config->input_path = input_path->count > 0 ? (char *)input_path->sval[0] : "/dev/null";
    _config->output_path = output_path->count > 0 ? (char *)output_path->sval[0] : "/dev/null";
    _config->error_path = error_path->count > 0 ? (char *)error_path->sval[0] : NULL;
    _config->log_path = log_path->count > 0 ? (char *)log_path->sval[0] : "sandbox.log";
    _config->io_mode = io_mode->count > 0 ? (char *)io_mode->sval[0] : NULL;
    _config->io_mode_memfd = _config->io_mode != NULL && strcmp(_config->io_mode, "memfd") == 0;
    _config->isolate_fs = isolate_fs->count > 0 && *isolate_fs->ival == 1;
    _config->work_readonly = work_readonly->count == 0 || *work_readonly->ival == 1;
    _config->memfd_input = -1;
    _config->memfd_output = -1;
    _config->memfd_error = -1;
    _config->status_fd = -1;
    _config->cgroup_root = cgroup_root->count > 0 ? (char *)cgroup_root->sval[0] : getenv("SDUOJ_CGROUP_ROOT");
    if (_config->cgroup_root == NULL) _config->cgroup_root = "/sys/fs/cgroup";
    _config->memfd_input_bytes = 0;

    _config->exe_args[0] = _config->exe_path;
    for (i = 1; i <= exe_args->count; i++)
        _config->exe_args[i] = (char *)exe_args->sval[i - 1];
    _config->exe_args[i] = NULL;

    for (i = 0; i < exe_envs->count; i++)
        _config->exe_envs[i] = (char *)exe_envs->sval[i];
    _config->exe_envs[i] = NULL;

    _config->seccomp_rules = seccomp_rules->count > 0 ? (char *)seccomp_rules->sval[0] : NULL;
    GetNobody(&nobody_uid, &nobody_gid);
    _config->uid = uid->count > 0 ? (uid_t)*uid->ival : nobody_uid;
    _config->gid = gid->count > 0 ? (gid_t)*gid->ival : nobody_gid;
}
