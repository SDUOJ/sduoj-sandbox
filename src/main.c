#include <stdio.h>
#include <inttypes.h>

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

    printf("{\"cpu_time\":%u,\"user_cpu_time\":%u,\"system_cpu_time\":%u,\"real_time\":%u,\"wall_time\":%u,\"memory\":%" PRIu64 ",\"signal\":%d,\"exit_code\":%d,\"error\":%d,\"result\":%d}\n",
           _result.cpu_time,
           _result.user_cpu_time,
           _result.system_cpu_time,
           _result.real_time,
           _result.wall_time,
           _result.memory,
           _result.signal,
           _result.exit_code,
           _result.error,
           _result.result);

    Halt(0);
}
