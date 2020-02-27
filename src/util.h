#ifndef SANDBOX_UTIL_H_
#define SANDBOX_UTIL_H_

#include "public.h"

/* get uid and pid of role `nobody` */
void GetNobody(int *uid, int *gid);

/* release arg table and exit */
void Halt(int exit_code);

/* receive unexcepted arg and hit help */
void UnexceptedArg();

/* print how to use sandbox */
void PrintUsage();

/* print current sandbox version */
void PrintVersion();
#endif //SANDBOX_UTIL_H_