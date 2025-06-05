#include "util.h"

#include <pwd.h>
#include <string.h>
#include <stdlib.h>

#include "parser.h"

void GetNobody(int *uid, int *gid)
{
    struct passwd *pwd = getpwnam("nobody");
    if (pwd != NULL)
    {
        *uid = pwd->pw_uid;
        *gid = pwd->pw_gid;
    }
    else
    {
        *uid = 65534;
        *gid = 65534;
    }
}

void Halt(int exit_code)
{
    arg_freetable(arg_table, sizeof(arg_table) / sizeof(arg_table[0]));
    exit(exit_code);
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
