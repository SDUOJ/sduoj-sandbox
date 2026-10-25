#include <stdio.h>
#include <seccomp.h>
#include <linux/seccomp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "../public.h"

int general_seccomp_rules(struct config *_config)
{
    /* The common filter checks clone's pthread flags. Keep this language
     * profile compatible with Python and JVM thread startup. */
    int syscalls_blacklist[] = {SCMP_SYS(fork), SCMP_SYS(vfork),
#ifdef __NR_execveat
                                SCMP_SYS(execveat)
#endif
    };
    int syscalls_blacklist_length = sizeof(syscalls_blacklist) / sizeof(int);
    scmp_filter_ctx ctx = NULL;
    // load seccomp rules
    ctx = seccomp_init(SCMP_ACT_ALLOW);
    if (!ctx)
    {
        return LOAD_SECCOMP_FAILED;
    }
    for (int i = 0; i < syscalls_blacklist_length; i++)
    {
        if (seccomp_rule_add(ctx, SCMP_ACT_KILL_PROCESS, syscalls_blacklist[i], 0) != 0)
        {
            return LOAD_SECCOMP_FAILED;
        }
    }
    // use SCMP_ACT_KILL for socket, python will be killed immediately
    if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EACCES), SCMP_SYS(socket), 0) != 0)
    {
        return LOAD_SECCOMP_FAILED;
    }
    (void)_config;
    if (seccomp_load(ctx) != 0)
    {
        return LOAD_SECCOMP_FAILED;
    }
    seccomp_release(ctx);
    return 0;
}
