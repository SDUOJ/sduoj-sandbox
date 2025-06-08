#ifndef SANDBOX_UTIL_H_
#define SANDBOX_UTIL_H_

#include "public.h"

/* get uid and gid of user `nobody` */
void GetNobody(int *uid, int *gid);

/* release arg table and exit */
void Halt(int exit_code);

/* receive unexcepted arg and hit help */
void UnexceptedArg();

/* print how to use sandbox */
void PrintUsage();

/* print current sandbox version */
void PrintVersion();

/* remove surrounding double quotes from a string */
char *TrimDoubleQuotes(char *str);
#endif //SANDBOX_UTIL_H_
