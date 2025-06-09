#include <stdio.h>
#include <seccomp.h>
#include <linux/seccomp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "../public.h"

int _c_cpp_seccomp_rules(struct config *_config, int allow_write_file)
{
int syscalls_whitelist[] = {SCMP_SYS(read), SCMP_SYS(pread64), SCMP_SYS(fstat),
                                SCMP_SYS(mmap), SCMP_SYS(mprotect),
                                SCMP_SYS(munmap), SCMP_SYS(uname),
                                SCMP_SYS(arch_prctl), SCMP_SYS(brk),
                                SCMP_SYS(access), SCMP_SYS(exit_group),
                                SCMP_SYS(close), SCMP_SYS(readlink),
                                SCMP_SYS(sysinfo), SCMP_SYS(write),
                                SCMP_SYS(writev), SCMP_SYS(lseek),
                                SCMP_SYS(clock_gettime),
                                SCMP_SYS(set_tid_address), SCMP_SYS(futex),
                                SCMP_SYS(set_robust_list), SCMP_SYS(rt_sigaction),
                                SCMP_SYS(rt_sigprocmask), SCMP_SYS(sigaltstack),
                                SCMP_SYS(getrandom), SCMP_SYS(rseq),
                                SCMP_SYS(newfstatat), SCMP_SYS(prlimit64),
                                SCMP_SYS(sysinfo), SCMP_SYS(readv),
                                SCMP_SYS(faccessat)};

    int syscalls_whitelist_length = sizeof(syscalls_whitelist) / sizeof(int);
    scmp_filter_ctx ctx = NULL;
    // load seccomp rules
    ctx = seccomp_init(SCMP_ACT_TRACE(1));
    if (!ctx)
    {
        return LOAD_SECCOMP_FAILED;
    }
    for (int i = 0; i < syscalls_whitelist_length; i++)
    {
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, syscalls_whitelist[i], 0) != 0)
        {
            return LOAD_SECCOMP_FAILED;
        }
    }
    // add extra rule for execve
    if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(execve), 1, SCMP_A0(SCMP_CMP_EQ, (scmp_datum_t)(_config->exe_path))) != 0)
    {
        return LOAD_SECCOMP_FAILED;
    }
    if (!allow_write_file)
    {
        // do not allow "w" and "rw"
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(open), 1, SCMP_CMP(1, SCMP_CMP_MASKED_EQ, O_WRONLY | O_RDWR, 0)) != 0)
        {
            return LOAD_SECCOMP_FAILED;
        }
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(openat), 1, SCMP_CMP(2, SCMP_CMP_MASKED_EQ, O_WRONLY | O_RDWR, 0)) != 0)
        {
            return LOAD_SECCOMP_FAILED;
        }
    }
    else
    {
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(open), 0) != 0)
        {
            return LOAD_SECCOMP_FAILED;
        }
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(dup), 0) != 0)
        {
            return LOAD_SECCOMP_FAILED;
        }
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(dup2), 0) != 0)
        {
            return LOAD_SECCOMP_FAILED;
        }
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(dup3), 0) != 0)
        {
            return LOAD_SECCOMP_FAILED;
        }
    }
    if (seccomp_load(ctx) != 0)
    {
        return LOAD_SECCOMP_FAILED;
    }
    seccomp_release(ctx);
    return 0;
}

int c_cpp_seccomp_rules(struct config *_config)
{
    return _c_cpp_seccomp_rules(_config, 0);
}

int c_cpp_file_io_seccomp_rules(struct config *_config)
{
    return _c_cpp_seccomp_rules(_config, 1);
}
