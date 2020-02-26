#include "util.h"

#include <pwd.h>
#include <string.h>
#include <stdlib.h>

#include "parser.h"
#include "stat.h"

void GetNobody(int* uid, int* gid) {
    struct passwd* pw;
    while((pw = getpwent())) {
        if(!strcmp(pw->pw_name, "nobody")) {
            *uid = pw->pw_uid;
            *gid = pw->pw_gid;
            return;
        }
    }
    /* can not find `nobody` */
    *uid = -1;
    *gid = -1;
    return;
}

void Halt(int exit_coide) {
    arg_freetable(arg_table, sizeof(arg_table) / sizeof(arg_table[0]));
    exit(exit_coide);
}

void UnexceptedArg() {
    arg_print_errors(stdout, end, PROJECT_NAME);
    printf("Try '%s --help' for more information.\n", PROJECT_NAME);
}

void PrintUsage() {
    printf("Usage: %s", PROJECT_NAME);
    arg_print_syntax(stdout, arg_table, "\n\n");
    arg_print_glossary(stdout, arg_table, "  %-25s %s\n");
}

void PrintVersion() {
    printf("Version: %d.%d.%d\n", (VERSION >> 16) & 0xff, (VERSION >> 8) & 0xff, VERSION & 0xff);
}