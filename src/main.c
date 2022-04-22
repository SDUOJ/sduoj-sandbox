#include <stdio.h>

#include "parser.h"
#include "public.h"
#include "util.h"
#include "container/container.h"

int main(int argc, char **argv)
{
    struct result _result;
    struct config _config;

    Initialize(argc, argv, &_config);

    Run(&_config, &_result);

    printf("{\"cpu_time\":%u,\"real_time\":%u,\"memory\":%lu,\"signal\":%d,\"exit_code\":%d,\"error\":%d,\"result\":%d}\n",
           _result.cpu_time,
           _result.real_time,
           _result.memory,
           _result.signal,
           _result.exit_code,
           _result.error,
           _result.result);

    Halt(0);
}
