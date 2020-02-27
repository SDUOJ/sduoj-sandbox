#ifndef JUDGE_UTIL_H_
#define JUDGE_UTIL_H_

#include "public.h"

/* get uid and pid of role `nobody` */
void GetNobody(int *uid, int *gid);

/* release arg table and exit */
void Halt(int exit_code);

/* receive unexcepted arg and hit help */
void UnexceptedArg();

/* print how to use judger */
void PrintUsage();

/* print current judger version */
void PrintVersion();
#endif //JUDGE_UTIL_H_