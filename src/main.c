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

    Examine(&_config, &_result);

    printf("{\"cpu_time\":%d,\"real_time\":%d,\"memory\":%d,\"signal\":%d,\"exit_code\":%d,\"error\":%d,\"result\":%d}\n",
           _result.cpu_time,
           _result.real_time,
           _result.memory,
           _result.signal,
           _result.exit_code,
           _result.error,
           _result.result);

    Halt(0);
}
