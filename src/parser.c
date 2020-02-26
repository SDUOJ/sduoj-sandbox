#include "parser.h"
#include "util.h"

#include <signal.h>

#define MAX_ERROR 10

void Initialize(int argc, char **argv)
{
    arg_table[0] = (help = arg_litn(NULL, "help", 0, 1, "Display help and exit."));
    arg_table[1] = (version = arg_litn(NULL, "version", 0, 1, "Display version info and exit."));
    arg_table[2] = (max_cpu_time = arg_intn(NULL, "max_cpu_time", INT_PLACEHOLDER, 0, 1, "Max cpu running time (ms)."));
    arg_table[3] = (max_real_time = arg_intn(NULL, "max_real_time", INT_PLACEHOLDER, 0, 1, "Max real running time (ms)."));
    arg_table[4] = (max_memory = arg_intn(NULL, "max_memory", INT_PLACEHOLDER, 0, 1, "Max memory (Kilobyte)."));
    arg_table[5] = (max_stack = arg_intn(NULL, "max_stack", INT_PLACEHOLDER, 0, 1, "Max stack size (Kilobyte, default 16384K)."));
    arg_table[6] = (max_process_number = arg_intn(NULL, "max_process_number", INT_PLACEHOLDER, 0, 1, "Max Process Number"));
    arg_table[7] = (max_output_size = arg_intn(NULL, "max_output_size", INT_PLACEHOLDER, 0, 1, "Max Output Size (byte)"));
    arg_table[8] = (exe_path = arg_str1(NULL, "exe_path", STR_PLACEHOLDER, "Executable file path."));
    arg_table[9] = (input_path = arg_strn(NULL, "input_path", STR_PLACEHOLDER, 0, 1, "Input file path."));
    arg_table[10] = (output_path = arg_strn(NULL, "output_path", STR_PLACEHOLDER, 0, 1, "Output file path."));
    arg_table[11] = (log_path = arg_strn(NULL, "log_path", STR_PLACEHOLDER, 0, 1, "Log file path."));
    arg_table[12] = (exe_args = arg_strn(NULL, "exe_args", STR_PLACEHOLDER, 0, 255, "Arguments for exectuable file."));
    arg_table[13] = (exe_envs = arg_strn(NULL, "exe_envs", STR_PLACEHOLDER, 0, 255, "Environments for executable file."));
    arg_table[14] = (seccomp_rules = arg_strn(NULL, "seccomp_rules", STR_PLACEHOLDER, 0, 1, "Seccomp rules."));
    arg_table[15] = (uid = arg_intn(NULL, "uid", INT_PLACEHOLDER, 0, 1, "UID for executable file (default `nobody`)."));
    arg_table[16] = (gid = arg_intn(NULL, "gid", INT_PLACEHOLDER, 0, 1, "GID for executable file (default `nobody`)"));
    arg_table[17] = (end = arg_end(MAX_ERROR));

    int nerrors = arg_parse(argc, argv, arg_table);

    if (help->count > 0)
    {
        PrintUsage();
        Halt(0);
    }

    if (version->count > 0)
    {
        PrintVersion();
        Halt(0);
    }

    if (nerrors > 0)
    {
        UnexceptedArg();
        Halt(INVALID_CONFIG);
    }

    signal(SIGINT, Halt);

    return;
}