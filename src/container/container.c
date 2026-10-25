#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <dirent.h>
#include <linux/capability.h>
#include <linux/securebits.h>
#include <poll.h>
#include <sched.h>
#include <seccomp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "container.h"
#include "filesystem.h"
#include "../logger/logger.h"
#include "../rules/seccomp_rules.h"

/* Ubuntu 18.04's headers predate Landlock.  The syscall ABI is stable and
 * these definitions keep the sandbox buildable on the old judging image. */
#ifndef __NR_landlock_create_ruleset
#if defined(__aarch64__) || defined(__x86_64__) || defined(__riscv)
#define __NR_landlock_create_ruleset 444
#define __NR_landlock_add_rule 445
#define __NR_landlock_restrict_self 446
#else
#error "Landlock syscall numbers are not defined for this architecture"
#endif
#endif

#ifndef LANDLOCK_RULE_TYPE_PATH_BENEATH
#define LANDLOCK_RULE_TYPE_PATH_BENEATH 1
#define LANDLOCK_CREATE_RULESET_VERSION (1U << 0)
#define LANDLOCK_ACCESS_FS_EXECUTE (1ULL << 0)
#define LANDLOCK_ACCESS_FS_WRITE_FILE (1ULL << 1)
#define LANDLOCK_ACCESS_FS_READ_FILE (1ULL << 2)
#define LANDLOCK_ACCESS_FS_READ_DIR (1ULL << 3)
#define LANDLOCK_ACCESS_FS_REMOVE_DIR (1ULL << 4)
#define LANDLOCK_ACCESS_FS_REMOVE_FILE (1ULL << 5)
#define LANDLOCK_ACCESS_FS_MAKE_CHAR (1ULL << 6)
#define LANDLOCK_ACCESS_FS_MAKE_DIR (1ULL << 7)
#define LANDLOCK_ACCESS_FS_MAKE_REG (1ULL << 8)
#define LANDLOCK_ACCESS_FS_MAKE_SOCK (1ULL << 9)
#define LANDLOCK_ACCESS_FS_MAKE_FIFO (1ULL << 10)
#define LANDLOCK_ACCESS_FS_MAKE_BLOCK (1ULL << 11)
#define LANDLOCK_ACCESS_FS_MAKE_SYM (1ULL << 12)
struct landlock_ruleset_attr {
    uint64_t handled_access_fs;
};
struct landlock_path_beneath_attr {
    uint64_t allowed_access;
    int32_t parent_fd;
};
#endif

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#define MFD_ALLOW_SEALING 0x0002U
#endif
#ifndef SYS_close_range
#define SYS_close_range 436
#endif
#ifndef F_ADD_SEALS
#define F_ADD_SEALS 1033
#define F_SEAL_SEAL 0x0001
#define F_SEAL_SHRINK 0x0002
#define F_SEAL_GROW 0x0004
#define F_SEAL_WRITE 0x0008
#endif

#define INPUT_COPY_BUFFER (64 * 1024)
#define WAIT_POLL_NS 1000000L
#define FS_ACCESS_READ (LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR)
#define FS_ACCESS_FILE (LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_READ_FILE)
#define FS_ACCESS_TMP (FS_ACCESS_READ | LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_REMOVE_DIR | LANDLOCK_ACCESS_FS_REMOVE_FILE | LANDLOCK_ACCESS_FS_MAKE_DIR | LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_MAKE_SYM | LANDLOCK_ACCESS_FS_MAKE_FIFO | LANDLOCK_ACCESS_FS_MAKE_SOCK)
#define CHILD_STACK_SIZE (1024 * 1024)

struct launch_status {
    int error;
    uint64_t start_ms;
    struct rusage setup_usage;
    pid_t host_pid;
    int complete;
    int child_status;
    struct rusage child_usage;
};

struct cgroup_run {
    char path[PATH_MAX];
    unsigned long long memory_oom_kill;
    unsigned long long pids_max;
    int active;
};

static uint64_t MonotonicMilliseconds(void);

static int WriteCgroupValue(const char *directory, const char *name, const char *value)
{
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/%s", directory, name) >= (int)sizeof(path)) return -1;
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    size_t length = strlen(value);
    ssize_t written = write(fd, value, length);
    close(fd);
    return written == (ssize_t)length ? 0 : -1;
}

static unsigned long long ReadCgroupEvent(const char *directory, const char *file_name, const char *key_name)
{
    char path[PATH_MAX], key[64];
    unsigned long long value = 0;
    if (snprintf(path, sizeof(path), "%s/%s", directory, file_name) >= (int)sizeof(path)) return 0;
    FILE *file = fopen(path, "r");
    if (file == NULL) return 0;
    while (fscanf(file, "%63s %llu", key, &value) == 2) {
        if (strcmp(key, key_name) == 0) {
            fclose(file);
            return value;
        }
    }
    fclose(file);
    return 0;
}

static int CreateRunCgroup(struct config *_config, struct cgroup_run *run)
{
    struct stat st;
    if (_config->cgroup_root == NULL || stat(_config->cgroup_root, &st) != 0 || !S_ISDIR(st.st_mode)) return -1;
    if (snprintf(run->path, sizeof(run->path), "%s/sduoj-%ld-%llu", _config->cgroup_root,
                 (long)getpid(), (unsigned long long)MonotonicMilliseconds()) >= (int)sizeof(run->path)) return -1;
    if (mkdir(run->path, 0700) != 0) return -1;
    char memory[64], pids[64];
    snprintf(memory, sizeof(memory), "%llu", (unsigned long long)_config->max_memory);
    snprintf(pids, sizeof(pids), "%llu", (unsigned long long)_config->max_process_number);
    if (WriteCgroupValue(run->path, "memory.max", memory) != 0 ||
        WriteCgroupValue(run->path, "pids.max", pids) != 0 ||
        WriteCgroupValue(run->path, "memory.swap.max", "0") != 0 ||
        WriteCgroupValue(run->path, "memory.oom.group", "1") != 0) {
        rmdir(run->path);
        return -1;
    }
    run->memory_oom_kill = ReadCgroupEvent(run->path, "memory.events", "oom_kill");
    run->pids_max = ReadCgroupEvent(run->path, "pids.events", "max");
    run->active = 1;
    return 0;
}

static void RemoveRunCgroup(struct cgroup_run *run)
{
    if (run->active) {
        rmdir(run->path);
        run->active = 0;
    }
}

static int AttachCgroup(const struct cgroup_run *run, pid_t pid)
{
    char value[32];
    snprintf(value, sizeof(value), "%ld", (long)pid);
    return WriteCgroupValue(run->path, "cgroup.procs", value);
}

static uint64_t MonotonicMilliseconds(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static int CreateMemfd(const char *name)
{
    return (int)syscall(__NR_memfd_create, name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
}

static int CopyInputToMemfd(const char *path, rlim_t limit, int *fd_out, uint64_t *bytes_out)
{
    int source = -1;
    int target = -1;
    char buffer[INPUT_COPY_BUFFER];
    uint64_t total = 0;

    source = open(path, O_RDONLY | O_CLOEXEC);
    if (source < 0)
        return -1;
    target = CreateMemfd("sduoj-input");
    if (target < 0)
        goto fail;

    for (;;) {
        ssize_t count = read(source, buffer, sizeof(buffer));
        if (count == 0)
            break;
        if (count < 0) {
            if (errno == EINTR)
                continue;
            goto fail;
        }
        if (total > (uint64_t)limit || (uint64_t)count > (uint64_t)limit - total) {
            errno = EFBIG;
            goto fail;
        }
        ssize_t written = 0;
        while (written < count) {
            ssize_t n = write(target, buffer + written, (size_t)(count - written));
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                goto fail;
            }
            written += n;
        }
        total += (uint64_t)count;
    }
    if (lseek(target, 0, SEEK_SET) < 0)
        goto fail;
    if (fcntl(target, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) < 0)
        goto fail;
    close(source);
    *fd_out = target;
    *bytes_out = total;
    return 0;

fail:
    close(source);
    if (target >= 0)
        close(target);
    return -1;
}

static int CreateEmptyInputMemfd(int *fd_out, uint64_t *bytes_out)
{
    int fd = CreateMemfd("sduoj-input");
    if (fd < 0)
        return -1;
    if (fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) < 0) {
        close(fd);
        return -1;
    }
    *fd_out = fd;
    *bytes_out = 0;
    return 0;
}

static int CreateOutputMemfd(const char *name)
{
    int fd = CreateMemfd(name);
    if (fd < 0)
        return -1;
    /* The child may grow this file, but may never shrink it.  RLIMIT_FSIZE
     * provides the hard byte ceiling for writes. */
    if (fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int CopyFdToPath(int source_fd, const char *path, rlim_t limit)
{
    int target = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    char buffer[INPUT_COPY_BUFFER];
    uint64_t total = 0;
    if (target < 0)
        return -1;
    struct stat target_stat;
    if (fstat(target, &target_stat) != 0 ||
        (S_ISREG(target_stat.st_mode) && fchmod(target, 0644) != 0))
        goto fail;
    if (lseek(source_fd, 0, SEEK_SET) < 0)
        goto fail;
    for (;;) {
        ssize_t count = read(source_fd, buffer, sizeof(buffer));
        if (count == 0)
            break;
        if (count < 0) {
            if (errno == EINTR)
                continue;
            goto fail;
        }
        if (total >= (uint64_t)limit)
            break;
        if ((uint64_t)count > (uint64_t)limit - total)
            count = (ssize_t)((uint64_t)limit - total);
        ssize_t written = 0;
        while (written < count) {
            ssize_t n = write(target, buffer + written, (size_t)(count - written));
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                goto fail;
            }
            written += n;
        }
        total += (uint64_t)count;
        if (total >= (uint64_t)limit)
            break;
    }
    close(target);
    return 0;
fail:
    close(target);
    return -1;
}

static int AddLandlockRule(int ruleset_fd, const char *path, uint64_t access, int directory, int required)
{
    int flags = O_PATH | O_CLOEXEC;
    int path_fd;
    struct landlock_path_beneath_attr rule;
    if (directory)
        flags |= O_DIRECTORY;
    path_fd = open(path, flags);
    if (path_fd < 0) {
        if (!required && (errno == ENOENT || errno == ENOTDIR || errno == EACCES))
            return 0;
        return -1;
    }
    memset(&rule, 0, sizeof(rule));
    rule.allowed_access = access;
    rule.parent_fd = path_fd;
    int result = (int)syscall(__NR_landlock_add_rule, ruleset_fd, LANDLOCK_RULE_TYPE_PATH_BENEATH, &rule, 0);
    close(path_fd);
    return result;
}

static void ParentDirectory(const char *path, char *out, size_t out_size)
{
    const char *slash = strrchr(path, '/');
    if (slash == NULL || slash == path) {
        snprintf(out, out_size, "/");
        return;
    }
    size_t length = (size_t)(slash - path);
    if (length >= out_size)
        length = out_size - 1;
    memcpy(out, path, length);
    out[length] = '\0';
}

static int AddExecutableRuntimeRule(int ruleset_fd, const char *exe_path)
{
    char root[PATH_MAX];
    char resolved[PATH_MAX];
    const char *path = realpath(exe_path, resolved) != NULL ? resolved : exe_path;
    const char *bin = strstr(path, "/bin/");
    if (bin != NULL && strncmp(path, "/opt/", 5) == 0) {
        size_t length = (size_t)(bin - path);
        if (length >= sizeof(root))
            return -1;
        memcpy(root, path, length);
        root[length] = '\0';
        return AddLandlockRule(ruleset_fd, root, FS_ACCESS_READ, 1, 1);
    }
    char parent[PATH_MAX];
    ParentDirectory(path, parent, sizeof(parent));
    if (AddLandlockRule(ruleset_fd, parent,
                        LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_READ_DIR,
                        1, 1) < 0)
        return -1;
    return AddLandlockRule(ruleset_fd, path, FS_ACCESS_FILE, 0, 1);
}

static int ApplyFilesystemIsolation(struct config *_config)
{
    if (!_config->isolate_fs)
        return 0;

    struct landlock_ruleset_attr ruleset = {
        .handled_access_fs = LANDLOCK_ACCESS_FS_EXECUTE |
                             LANDLOCK_ACCESS_FS_WRITE_FILE |
                             LANDLOCK_ACCESS_FS_READ_FILE |
                             LANDLOCK_ACCESS_FS_READ_DIR |
                             LANDLOCK_ACCESS_FS_REMOVE_DIR |
                             LANDLOCK_ACCESS_FS_REMOVE_FILE |
                             LANDLOCK_ACCESS_FS_MAKE_DIR |
                             LANDLOCK_ACCESS_FS_MAKE_REG |
                             LANDLOCK_ACCESS_FS_MAKE_SOCK |
                             LANDLOCK_ACCESS_FS_MAKE_FIFO |
                             LANDLOCK_ACCESS_FS_MAKE_SYM |
                             LANDLOCK_ACCESS_FS_MAKE_CHAR |
                             LANDLOCK_ACCESS_FS_MAKE_BLOCK};
    int ruleset_fd = (int)syscall(__NR_landlock_create_ruleset, &ruleset, sizeof(ruleset), 0);
    if (ruleset_fd < 0)
        return -1;

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
        goto fail;
    if (_config->work_readonly) {
        /* The mount namespace already contains only program artifacts in
         * cwd. A recursive read grant also covers packaged Java classes. */
        if (AddLandlockRule(ruleset_fd, cwd, FS_ACCESS_READ, 1, 1) < 0)
            goto fail;
    } else if (AddLandlockRule(ruleset_fd, cwd, FS_ACCESS_TMP, 1, 1) < 0) {
        goto fail;
    }
    if (!_config->work_readonly &&
        (AddLandlockRule(ruleset_fd, "/tmp", FS_ACCESS_TMP, 1, 1) < 0 ||
         AddLandlockRule(ruleset_fd, "/usr/include", FS_ACCESS_READ, 1, 0) < 0))
        goto fail;
    for (int i = 1; i <= MAX_ARG && _config->exe_args[i] != NULL; i++) {
        struct stat st;
        if (stat(_config->exe_args[i], &st) == 0 && S_ISREG(st.st_mode) &&
            AddLandlockRule(ruleset_fd, _config->exe_args[i], FS_ACCESS_FILE, 0, 1) < 0)
            goto fail;
    }

    /* These are the read-only runtime surfaces needed by glibc, Python and
     * the JVM.  They contain no judge data. */
    const char *runtime_dirs[] = {
        "/bin", "/sbin", "/usr/bin", "/usr/sbin", "/lib", "/lib32", "/lib64",
        "/usr/lib", "/usr/lib32", "/usr/lib64", "/usr/share", "/etc", "/proc", "/dev"
    };
    for (size_t i = 0; i < sizeof(runtime_dirs) / sizeof(runtime_dirs[0]); i++) {
        if (AddLandlockRule(ruleset_fd, runtime_dirs[i], FS_ACCESS_READ, 1, 0) < 0)
            goto fail;
    }
    if (AddExecutableRuntimeRule(ruleset_fd, _config->exe_path) < 0)
        goto fail;
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
        goto fail;
    if (syscall(__NR_landlock_restrict_self, ruleset_fd, 0) != 0)
        goto fail;
    close(ruleset_fd);
    return 0;
fail:
    close(ruleset_fd);
    return -1;
}

static int InstallNetworkDeny(int allow_processes)
{
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ALLOW);
    if (ctx == NULL)
        return -1;
    if (seccomp_attr_set(ctx, SCMP_FLTATR_ACT_BADARCH, SCMP_ACT_KILL_PROCESS) != 0)
        goto fail;
    const int calls[] = {
        SCMP_SYS(socket), SCMP_SYS(socketpair), SCMP_SYS(connect), SCMP_SYS(bind),
        SCMP_SYS(listen), SCMP_SYS(accept), SCMP_SYS(accept4), SCMP_SYS(sendto),
        SCMP_SYS(sendmsg), SCMP_SYS(sendmmsg), SCMP_SYS(recvfrom), SCMP_SYS(recvmsg),
        SCMP_SYS(recvmmsg)
    };
    for (size_t i = 0; i < sizeof(calls) / sizeof(calls[0]); i++) {
        if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EACCES), calls[i], 0) != 0) {
            seccomp_release(ctx);
            return -1;
        }
    }
    /* Allow JVM/Python threads, never a second process.  glibc falls back
     * from clone3 to clone when clone3 reports ENOSYS. */
    const unsigned long thread_required = CLONE_VM | CLONE_SIGHAND | CLONE_THREAD;
    const unsigned long thread_allowed = thread_required | CLONE_FS | CLONE_FILES | CLONE_SYSVSEM |
            CLONE_SETTLS | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID | CLONE_CHILD_SETTID;
    if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOSYS), SCMP_SYS(clone3), 0) != 0)
        goto fail;
    /* Masked comparisons cannot express inequality. Reject every missing
     * required flag and every flag outside the pthread set separately. */
    for (unsigned int bit = 0; !allow_processes && bit < sizeof(unsigned long) * CHAR_BIT; bit++) {
        unsigned long mask = 1UL << bit;
        if (((thread_required & mask) != 0 &&
             seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(clone), 1,
                              SCMP_A0(SCMP_CMP_MASKED_EQ, mask, 0)) != 0) ||
            ((thread_allowed & mask) == 0 &&
             seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(clone), 1,
                              SCMP_A0(SCMP_CMP_MASKED_EQ, mask, mask)) != 0))
            goto fail;
    }
    const int forbidden[] = {
        SCMP_SYS(ptrace), SCMP_SYS(process_vm_readv),
        SCMP_SYS(process_vm_writev), SCMP_SYS(pidfd_getfd), SCMP_SYS(setns), SCMP_SYS(unshare),
        SCMP_SYS(mount), SCMP_SYS(umount2), SCMP_SYS(pivot_root), SCMP_SYS(chroot),
        SCMP_SYS(setpgid), SCMP_SYS(setsid), SCMP_SYS(bpf), SCMP_SYS(perf_event_open),
        SCMP_SYS(keyctl), SCMP_SYS(add_key), SCMP_SYS(request_key), SCMP_SYS(userfaultfd),
        SCMP_SYS(io_uring_setup), SCMP_SYS(io_uring_enter), SCMP_SYS(io_uring_register),
        SCMP_SYS(open_by_handle_at), SCMP_SYS(reboot),
        SCMP_SYS(process_madvise), SCMP_SYS(process_mrelease),
        SCMP_SYS(tkill), SCMP_SYS(rt_sigqueueinfo), SCMP_SYS(rt_tgsigqueueinfo),
        SCMP_SYS(pidfd_send_signal), SCMP_SYS(pidfd_open), SCMP_SYS(kcmp),
        SCMP_SYS(open_tree), SCMP_SYS(move_mount), SCMP_SYS(fsopen), SCMP_SYS(fsconfig),
        SCMP_SYS(fsmount), SCMP_SYS(fspick), SCMP_SYS(mount_setattr),
        SCMP_SYS(kexec_load), SCMP_SYS(kexec_file_load), SCMP_SYS(init_module),
        SCMP_SYS(finit_module), SCMP_SYS(delete_module), SCMP_SYS(swapon), SCMP_SYS(swapoff)
    };
    for (size_t i = 0; i < sizeof(forbidden) / sizeof(forbidden[0]); i++)
        if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), forbidden[i], 0) != 0)
            goto fail;
    if (!allow_processes) {
        const int shared_memory[] = {SCMP_SYS(memfd_create), SCMP_SYS(shmget), SCMP_SYS(shmat),
            SCMP_SYS(shmctl), SCMP_SYS(mq_open), SCMP_SYS(mq_unlink),
            SCMP_SYS(pipe), SCMP_SYS(pipe2)};
        for (size_t i = 0; i < sizeof(shared_memory) / sizeof(shared_memory[0]); i++)
            if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), shared_memory[i], 0) != 0)
                goto fail;
    }
    const int scheduling[] = {SCMP_SYS(sched_setparam), SCMP_SYS(sched_setscheduler),
                             SCMP_SYS(sched_setattr), SCMP_SYS(get_robust_list)};
    for (size_t i = 0; i < sizeof(scheduling) / sizeof(scheduling[0]); i++)
        if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), scheduling[i], 1,
                             SCMP_A0(SCMP_CMP_NE, 0)) != 0)
            goto fail;
    const int fd_controls[] = {F_SETOWN, F_SETSIG, F_SETLEASE, F_NOTIFY, F_SETPIPE_SZ, F_SETOWN_EX};
    for (size_t i = 0; i < sizeof(fd_controls) / sizeof(fd_controls[0]); i++)
        if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(fcntl), 1,
                             SCMP_A1(SCMP_CMP_EQ, fd_controls[i])) != 0)
            goto fail;
    if (!allow_processes &&
        (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(fork), 0) != 0 ||
         seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(vfork), 0) != 0))
        goto fail;
    pid_t self = getpid();
    if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(kill), 1,
                         SCMP_A0(SCMP_CMP_NE, self)) != 0 ||
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(tgkill), 1,
                         SCMP_A0(SCMP_CMP_NE, self)) != 0)
        goto fail;
    if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(prlimit64), 1,
                         SCMP_A0(SCMP_CMP_NE, 0)) != 0 ||
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(sched_setaffinity), 1,
                         SCMP_A0(SCMP_CMP_NE, 0)) != 0 ||
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(setpriority), 0) != 0 ||
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(prctl), 1,
                         SCMP_A0(SCMP_CMP_EQ, PR_SET_PDEATHSIG)) != 0)
        goto fail;
    int result = seccomp_load(ctx);
    seccomp_release(ctx);
    return result;
fail:
    seccomp_release(ctx);
    return -1;
}

void InitResult(struct result *_result)
{
    memset(_result, 0, sizeof(*_result));
}

void CheckArgs(FILE *log_fp, struct config *_config, struct result *_result)
{
    (void)_result;
    if (getuid() != 0)
        ERROR_EXIT(ROOT_REQUIRED);
    if (!_config->io_mode_memfd || !_config->isolate_fs ||
        (_config->seccomp_rules != NULL && strcmp(_config->seccomp_rules, "c_cpp_file_io") == 0) ||
        _config->max_cpu_time < 1 || _config->max_real_time < 1 || _config->max_stack < 1 ||
        _config->max_memory < 1 || _config->max_process_number < 1 || _config->max_output_size < 1 ||
        _config->max_io_input_size < 1 || !LIMITED(_config->max_cpu_time) ||
        !LIMITED(_config->max_real_time) || !LIMITED(_config->max_memory) ||
        !LIMITED(_config->max_stack) || !LIMITED(_config->max_process_number) ||
        _config->max_process_number > INT32_MAX ||
        !LIMITED(_config->max_output_size) || !LIMITED(_config->max_io_input_size) ||
        _config->max_output_size >= INT64_MAX || _config->max_cpu_time > INT32_MAX ||
        _config->max_real_time > INT32_MAX)
        ERROR_EXIT(INVALID_CONFIG);
}

static void ChildError(FILE *log_fp, struct config *_config, int error_code)
{
    struct launch_status failed = {.error = error_code};
    if (_config->status_fd >= 0) {
        ssize_t written = write(_config->status_fd, &failed, sizeof(failed));
        (void)written;
    }
    LOG_FATAL(log_fp, "System errno: %s; Internal errno: %d", strerror(errno), error_code);
    if (_config->memfd_input >= 0) close(_config->memfd_input);
    if (_config->memfd_output >= 0) close(_config->memfd_output);
    if (_config->memfd_error >= 0) close(_config->memfd_error);
    _exit(EXIT_FAILURE);
}

static void ChildProcess(FILE *log_fp, struct config *_config, pid_t parent_pid, const char *root)
{
    char host_pid_text[32];
    ssize_t host_pid_length = readlink("/proc/self", host_pid_text, sizeof(host_pid_text) - 1);
    if (host_pid_length < 1) ChildError(log_fp, _config, FORK_FAILED);
    host_pid_text[host_pid_length] = '\0';
    pid_t host_pid = (pid_t)strtol(host_pid_text, NULL, 10);
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() != parent_pid)
        ChildError(log_fp, _config, PARENT_DEATH_SIGNAL_FAILED);
    if (prctl(PR_SET_DUMPABLE, 0) != 0)
        ChildError(log_fp, _config, SETUID_FAILED);
    if (SetupFilesystem(_config, root) != 0)
        ChildError(log_fp, _config, LOAD_FS_ISOLATION_FAILED);
    struct rlimit no_core = {0, 0}, no_locked_memory = {0, 0}, descriptors = {256, 256};
    if (setrlimit(RLIMIT_CORE, &no_core) != 0 || setrlimit(RLIMIT_MEMLOCK, &no_locked_memory) != 0 ||
        setrlimit(RLIMIT_NOFILE, &descriptors) != 0)
        ChildError(log_fp, _config, SETRLIMIT_FAILED);

    if (LIMITED(_config->max_stack)) {
        struct rlimit limit = {_config->max_stack, _config->max_stack};
        if (setrlimit(RLIMIT_STACK, &limit) != 0) ChildError(log_fp, _config, SETRLIMIT_FAILED);
    }
    /* Virtual reservations are not resident memory. An arbitrary multiple
     * of the RSS limit makes Python allocations fail early and breaks JVM
     * reservations. The mandatory per-run memory.max and RSS watchdog
     * enforce the physical memory limit, including shared/kernel pages. */
    if (LIMITED(_config->max_cpu_time)) {
        /* The precise CPU watchdog excludes setup. This coarse kernel
         * backup leaves one second for namespace/filter construction. */
        rlim_t seconds = (_config->max_cpu_time + 999) / 1000 + 1;
        struct rlimit limit = {seconds, seconds};
        if (setrlimit(RLIMIT_CPU, &limit) != 0) ChildError(log_fp, _config, SETRLIMIT_FAILED);
    }
    /* RLIMIT_NPROC counts every task of the shared judge UID, including
     * concurrent submissions. Per-run cgroup pids.max is the hard limit. */
    if (LIMITED(_config->max_output_size)) {
        rlim_t file_size = _config->max_output_size == RLIM_INFINITY - 1
                ? RLIM_INFINITY
                : _config->max_output_size + 1;
        struct rlimit limit = {file_size, file_size};
        if (setrlimit(RLIMIT_FSIZE, &limit) != 0) ChildError(log_fp, _config, SETRLIMIT_FAILED);
    }

    if (dup2(_config->memfd_input, STDIN_FILENO) < 0 || dup2(_config->memfd_output, STDOUT_FILENO) < 0)
        ChildError(log_fp, _config, DUP2_FAILED);
    if (_config->memfd_error >= 0) {
        if (dup2(_config->memfd_error, STDERR_FILENO) < 0)
            ChildError(log_fp, _config, DUP2_FAILED);
    } else if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
        ChildError(log_fp, _config, DUP2_FAILED);
    }
    /* Inherited descriptors must not provide a path out of the new root.
     * CLOEXEC preserves our private setup/error channel until exec. */
    if (syscall(SYS_close_range, 3U, ~0U, 4U /* CLOSE_RANGE_CLOEXEC */) != 0)
        ChildError(log_fp, _config, DUP2_FAILED);

    if (ApplyFilesystemIsolation(_config) != 0)
        ChildError(log_fp, _config, LOAD_FS_ISOLATION_FAILED);
    if (geteuid() == 0 && prctl(PR_SET_SECUREBITS,
            SECBIT_NOROOT | SECBIT_NOROOT_LOCKED, 0, 0, 0) != 0)
        ChildError(log_fp, _config, SETUID_FAILED);
    gid_t groups[] = {_config->gid};
    if (_config->gid != (gid_t)-1 && (setgid(_config->gid) != 0 || setgroups(1, groups) != 0))
        ChildError(log_fp, _config, SETUID_FAILED);
    if (_config->uid != (uid_t)-1 && setuid(_config->uid) != 0)
        ChildError(log_fp, _config, SETUID_FAILED);

    struct __user_cap_header_struct cap_header = {_LINUX_CAPABILITY_VERSION_3, 0};
    struct __user_cap_data_struct cap_data[2] = {{0}, {0}};
    if (syscall(SYS_capset, &cap_header, cap_data) != 0)
        ChildError(log_fp, _config, SETUID_FAILED);
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() != parent_pid)
        ChildError(log_fp, _config, PARENT_DEATH_SIGNAL_FAILED);
    if (InstallNetworkDeny(!_config->work_readonly) != 0)
        ChildError(log_fp, _config, LOAD_SECCOMP_FAILED);

    if (_config->seccomp_rules != NULL) {
        int result = LOAD_SECCOMP_FAILED;
        if (strcmp("c_cpp", _config->seccomp_rules) == 0) result = c_cpp_seccomp_rules(_config);
        else if (strcmp("c_cpp_file_io", _config->seccomp_rules) == 0) result = c_cpp_file_io_seccomp_rules(_config);
        else if (strcmp("general", _config->seccomp_rules) == 0) result = general_seccomp_rules(_config);
        if (result != SUCCESS) ChildError(log_fp, _config, LOAD_SECCOMP_FAILED);
    }
    struct launch_status ready = {.start_ms = MonotonicMilliseconds(), .host_pid = host_pid};
    if (signal(SIGPIPE, SIG_DFL) == SIG_ERR)
        ChildError(log_fp, _config, EXECVE_FAILED);
    if (getrusage(RUSAGE_SELF, &ready.setup_usage) != 0 ||
        write(_config->status_fd, &ready, sizeof(ready)) != sizeof(ready))
        ChildError(log_fp, _config, WAIT_FAILED);
    execve(_config->exe_path, _config->exe_args, _config->exe_envs);
    ChildError(log_fp, _config, EXECVE_FAILED);
}

struct child_arguments {
    FILE *log_fp;
    struct config *config;
    pid_t parent_pid;
    const char *root;
    int status_read_fd;
    int gate_read_fd;
    int gate_write_fd;
};

static int CloneChild(void *argument)
{
    struct child_arguments *child = argument;
    close(child->status_read_fd);
    close(child->gate_write_fd);
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || prctl(PR_SET_DUMPABLE, 0) != 0 ||
        setpgid(0, 0) != 0)
        ChildError(child->log_fp, child->config, PARENT_DEATH_SIGNAL_FAILED);
    char ready;
    ssize_t count;
    do { count = read(child->gate_read_fd, &ready, 1); } while (count < 0 && errno == EINTR);
    close(child->gate_read_fd);
    if (count != 1 || getppid() != 0)
        ChildError(child->log_fp, child->config, PARENT_DEATH_SIGNAL_FAILED);
    /* PID 1 remains a tiny trusted reaper. Running the submission as PID 2
     * preserves normal default-signal semantics (namespace PID 1 ignores
     * some default fatal signals). */
    pid_t program = fork();
    if (program < 0) ChildError(child->log_fp, child->config, FORK_FAILED);
    if (program == 0) ChildProcess(child->log_fp, child->config, 1, child->root);
    struct launch_status finished = {.complete = 1};
    while (wait4(program, &finished.child_status, 0, &finished.child_usage) < 0) {
        if (errno != EINTR) ChildError(child->log_fp, child->config, WAIT_FAILED);
    }
    kill(-1, SIGKILL);
    while (waitpid(-1, NULL, 0) > 0 || errno == EINTR) {}
    if (write(child->config->status_fd, &finished, sizeof(finished)) != sizeof(finished))
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

static uint64_t UsageMicroseconds(const struct timeval *time)
{
    return (uint64_t)time->tv_sec * 1000000 + time->tv_usec;
}

static int WaitChild(pid_t child_pid, struct config *_config, struct rusage *usage, int *status,
                     int *timed_out, int status_fd, struct launch_status *launch)
{
    uint64_t setup_deadline = MonotonicMilliseconds() + 10000;
    uint64_t deadline = setup_deadline;
    clockid_t cpu_clock;
    int have_cpu_clock = 0;
    char statm_path[64];
    snprintf(statm_path, sizeof(statm_path), "/proc/%ld/statm", (long)child_pid);
    long page_size = sysconf(_SC_PAGESIZE);
    *timed_out = 0;
    for (;;) {
        struct launch_status message;
        ssize_t count;
        while ((count = read(status_fd, &message, sizeof(message))) == sizeof(message)) {
            if (message.complete) {
                launch->complete = 1;
                launch->child_status = message.child_status;
                launch->child_usage = message.child_usage;
                continue;
            }
            *launch = message;
            if (launch->start_ms != 0) {
                deadline = launch->start_ms + _config->max_real_time;
                have_cpu_clock = clock_getcpuclockid(launch->host_pid, &cpu_clock) == 0;
                snprintf(statm_path, sizeof(statm_path), "/proc/%ld/statm", (long)launch->host_pid);
            }
        }
        pid_t result = wait4(child_pid, status, WNOHANG, usage);
        if (result == child_pid)
            return 0;
        if (result < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        struct timespec cpu_time;
        uint64_t setup_cpu_us = UsageMicroseconds(&launch->setup_usage.ru_utime) +
                                UsageMicroseconds(&launch->setup_usage.ru_stime);
        if (launch->start_ms != 0 && have_cpu_clock &&
            clock_gettime(cpu_clock, &cpu_time) == 0 &&
            (uint64_t)cpu_time.tv_sec * 1000000 + (uint64_t)cpu_time.tv_nsec / 1000 >=
                setup_cpu_us + _config->max_cpu_time * 1000)
            *timed_out = CPU_TIME_LIMIT_EXCEEDED;
        if (LIMITED(_config->max_memory)) {
            FILE *statm = fopen(statm_path, "r");
            unsigned long pages, resident;
            if (statm != NULL) {
                if (fscanf(statm, "%lu %lu", &pages, &resident) == 2 &&
                    (uint64_t)resident * (uint64_t)page_size > _config->max_memory)
                    *timed_out = MEMORY_LIMIT_EXCEEDED;
                fclose(statm);
            }
        }
        if (*timed_out == 0 && MonotonicMilliseconds() >= deadline)
            *timed_out = REAL_TIME_LIMIT_EXCEEDED;
        if (launch->start_ms == 0 && MonotonicMilliseconds() >= setup_deadline)
            launch->error = LOAD_FS_ISOLATION_FAILED;
        if (*timed_out != 0) {
            /* Let PID 1 wait4 the program and relay its real rusage. Killing
             * the reaper first loses the program's CPU/RSS on watchdog exits. */
            if (launch->host_pid > 0) kill(launch->host_pid, SIGKILL);
            else {
                kill(-child_pid, SIGKILL);
                kill(child_pid, SIGKILL);
            }
            while (wait4(child_pid, status, 0, usage) < 0 && errno == EINTR) {}
            return 0;
        }
        struct timespec pause_time = {0, WAIT_POLL_NS};
        nanosleep(&pause_time, NULL);
    }
}

static uint32_t ExecutionMilliseconds(const struct timeval *total, const struct timeval *setup)
{
    uint64_t all_us = UsageMicroseconds(total), setup_us = UsageMicroseconds(setup);
    uint64_t ms = all_us > setup_us ? (all_us - setup_us) / 1000 : 0;
    return ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
}

static void GenerateResult(struct config *_config, struct result *_result, struct rusage *usage, int status, struct launch_status *launch, int timed_out)
{
    uint64_t now = MonotonicMilliseconds();
    uint64_t start_ms = launch->start_ms == 0 ? now : launch->start_ms;
    uint64_t wall = now >= start_ms ? now - start_ms : 0;
    _result->real_time = wall > UINT32_MAX ? UINT32_MAX : (uint32_t)wall;
    _result->wall_time = _result->real_time;
    _result->user_cpu_time = ExecutionMilliseconds(&usage->ru_utime, &launch->setup_usage.ru_utime);
    _result->system_cpu_time = ExecutionMilliseconds(&usage->ru_stime, &launch->setup_usage.ru_stime);
    uint64_t cpu = (uint64_t)_result->user_cpu_time + _result->system_cpu_time;
    _result->cpu_time = cpu > UINT32_MAX ? UINT32_MAX : (uint32_t)cpu;
    _result->memory = (uint64_t)usage->ru_maxrss * 1024ULL;

    if (WIFSIGNALED(status)) {
        _result->signal = WTERMSIG(status);
        if (_result->signal == SIGXFSZ) _result->result = OUTPUT_LIMIT_EXCEEDED;
        else if (_result->signal == SIGXCPU) _result->result = CPU_TIME_LIMIT_EXCEEDED;
        else if (timed_out) _result->result = timed_out;
        else if (_result->signal == SIGSEGV && LIMITED(_config->max_memory) && _result->memory > _config->max_memory) _result->result = MEMORY_LIMIT_EXCEEDED;
        else _result->result = RUNTIME_ERROR;
    } else if (WIFEXITED(status)) {
        _result->exit_code = WEXITSTATUS(status);
        _result->result = _result->exit_code == 0 ? SUCCESS : RUNTIME_ERROR;
        if (LIMITED(_config->max_memory) && _result->memory > _config->max_memory) _result->result = MEMORY_LIMIT_EXCEEDED;
        if (timed_out || (LIMITED(_config->max_real_time) && _result->real_time > _config->max_real_time)) _result->result = timed_out ? timed_out : REAL_TIME_LIMIT_EXCEEDED;
        if (LIMITED(_config->max_cpu_time) && _result->cpu_time > _config->max_cpu_time) _result->result = CPU_TIME_LIMIT_EXCEEDED;
    } else {
        _result->result = SYSTEM_ERROR;
    }
    if (_result->result != SYSTEM_ERROR) {
        if (LIMITED(_config->max_memory) && _result->memory > _config->max_memory)
            _result->result = MEMORY_LIMIT_EXCEEDED;
        else if (LIMITED(_config->max_cpu_time) && _result->cpu_time >= _config->max_cpu_time)
            _result->result = CPU_TIME_LIMIT_EXCEEDED;
    }
    if (launch->error != 0 || launch->start_ms == 0) {
        _result->error = launch->error ? launch->error : EXECVE_FAILED;
        _result->result = SYSTEM_ERROR;
    }
}

void Run(struct config *_config, struct result *_result)
{
    FILE *log_fp = LogOpen(_config->log_path);
    struct stat input_stat, output_stat;
    struct rusage usage = {0};
    struct launch_status launch = {0};
    int status = 0, timed_out = 0, copy_error = 0, root_created = 0;
    int status_pipe[2] = {-1, -1}, gate[2] = {-1, -1};
    struct cgroup_run cgroup = {0};
    char root[] = "/tmp/sduoj-sandbox-XXXXXX";
    InitResult(_result);
    CheckArgs(log_fp, _config, _result);
    if (_result->result == SYSTEM_ERROR)
        return;
    /* A child that fails before the launch gate opens must still yield a
     * JSON system error, rather than terminate its supervisor with SIGPIPE. */
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) goto io_failure;

    if (strcmp(_config->input_path, "/dev/null") != 0 &&
        (stat(_config->input_path, &input_stat) != 0 || !S_ISREG(input_stat.st_mode) ||
         (uint64_t)input_stat.st_size > (uint64_t)_config->max_io_input_size))
        goto io_failure;
    int input_result = strcmp(_config->input_path, "/dev/null") == 0
            ? CreateEmptyInputMemfd(&_config->memfd_input, &_config->memfd_input_bytes)
            : CopyInputToMemfd(_config->input_path, _config->max_io_input_size,
                               &_config->memfd_input, &_config->memfd_input_bytes);
    if (input_result != 0 ||
        (_config->memfd_output = CreateOutputMemfd("sduoj-output")) < 0 ||
        (_config->error_path != NULL &&
         (_config->memfd_error = CreateOutputMemfd("sduoj-error")) < 0))
        goto io_failure;
    if (mkdtemp(root) == NULL) {
        _result->error = LOAD_FS_ISOLATION_FAILED;
        _result->result = SYSTEM_ERROR;
        goto cleanup;
    }
    root_created = 1;
    if (CreateRunCgroup(_config, &cgroup) != 0) {
        _result->error = LOAD_CGROUP_FAILED;
        _result->result = SYSTEM_ERROR;
        goto cleanup;
    }
    LOG_INFO(log_fp, "cgroup path=%s memory.max=%llu pids.max=%llu", cgroup.path,
             (unsigned long long)_config->max_memory, (unsigned long long)_config->max_process_number);
    if (pipe2(status_pipe, O_CLOEXEC | O_NONBLOCK) != 0 || pipe2(gate, O_CLOEXEC) != 0)
        goto io_failure;
    if (prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) != 0)
        goto io_failure;
    _config->status_fd = status_pipe[1];
    pid_t parent_pid = getpid();
    struct child_arguments child_arguments = {
        .log_fp = log_fp, .config = _config, .parent_pid = parent_pid, .root = root,
        .status_read_fd = status_pipe[0], .gate_read_fd = gate[0], .gate_write_fd = gate[1]};
    void *child_stack = malloc(CHILD_STACK_SIZE);
    if (child_stack == NULL) {
        _result->error = FORK_FAILED;
        _result->result = SYSTEM_ERROR;
        goto cleanup;
    }
    pid_t child_pid = clone(CloneChild, (char *)child_stack + CHILD_STACK_SIZE,
                            CLONE_NEWPID | SIGCHLD, &child_arguments);
    if (child_pid < 0) {
        free(child_stack);
        _result->error = FORK_FAILED;
        _result->result = SYSTEM_ERROR;
        goto cleanup;
    }
    if (AttachCgroup(&cgroup, child_pid) != 0) {
        kill(child_pid, SIGKILL);
        _result->error = LOAD_CGROUP_FAILED;
        _result->result = SYSTEM_ERROR;
    }
    close(gate[0]);
    gate[0] = -1;
    if (_result->result != SYSTEM_ERROR && write(gate[1], "1", 1) != 1) {
        _result->error = FORK_FAILED;
        _result->result = SYSTEM_ERROR;
    }
    close(gate[1]);
    gate[1] = -1;
    close(status_pipe[1]);
    status_pipe[1] = -1;
    if (setpgid(child_pid, child_pid) != 0 && errno != EACCES && errno != ESRCH) {
        _result->error = FORK_FAILED;
        _result->result = SYSTEM_ERROR;
    } else if (WaitChild(child_pid, _config, &usage, &status, &timed_out,
                          status_pipe[0], &launch) != 0) {
        _result->error = WAIT_FAILED;
        _result->result = SYSTEM_ERROR;
    }
    kill(-child_pid, SIGKILL);
    if (_result->result == SYSTEM_ERROR) {
        kill(child_pid, SIGKILL);
        while (wait4(child_pid, &status, 0, &usage) < 0 && errno == EINTR) {}
    } else {
        /* Drain any status written between the last nonblocking read and
         * wait4, especially an execve failure following the ready record. */
        struct launch_status message;
        while (read(status_pipe[0], &message, sizeof(message)) == sizeof(message)) {
            if (message.complete) {
                launch.complete = 1;
                launch.child_status = message.child_status;
                launch.child_usage = message.child_usage;
            } else launch = message;
        }
        if (launch.complete) {
            status = launch.child_status;
            usage = launch.child_usage;
        }
        GenerateResult(_config, _result, &usage, status, &launch, timed_out);
    }
    free(child_stack);
    unsigned long long oom_kill = ReadCgroupEvent(cgroup.path, "memory.events", "oom_kill");
    unsigned long long pids_max = ReadCgroupEvent(cgroup.path, "pids.events", "max");
    LOG_INFO(log_fp, "cgroup memory_oom_kill=%llu pids_max=%llu", oom_kill, pids_max);
    if (oom_kill > cgroup.memory_oom_kill &&
        _result->result != SYSTEM_ERROR) {
        _result->result = MEMORY_LIMIT_EXCEEDED;
        /* memory.oom.group may also kill namespace PID 1 before it can
         * relay wait4's rusage. The cgroup retains aggregate usage. */
        unsigned long long peak = 0;
        char peak_path[PATH_MAX];
        if (snprintf(peak_path, sizeof(peak_path), "%s/memory.peak", cgroup.path) < (int)sizeof(peak_path)) {
            FILE *peak_file = fopen(peak_path, "r");
            if (peak_file != NULL) {
                if (fscanf(peak_file, "%llu", &peak) == 1 && peak > _result->memory)
                    _result->memory = peak;
                fclose(peak_file);
            }
        }
        unsigned long long user_us = ReadCgroupEvent(cgroup.path, "cpu.stat", "user_usec");
        unsigned long long system_us = ReadCgroupEvent(cgroup.path, "cpu.stat", "system_usec");
        unsigned long long setup_user_us = UsageMicroseconds(&launch.setup_usage.ru_utime);
        unsigned long long setup_system_us = UsageMicroseconds(&launch.setup_usage.ru_stime);
        _result->user_cpu_time = user_us > setup_user_us ? (user_us - setup_user_us) / 1000 : 0;
        _result->system_cpu_time = system_us > setup_system_us ? (system_us - setup_system_us) / 1000 : 0;
        _result->cpu_time = _result->user_cpu_time + _result->system_cpu_time;
    }
    if (pids_max > cgroup.pids_max &&
        _result->result == SUCCESS)
        _result->result = RUNTIME_ERROR;
    /* Compiler/checker descendants cannot change session or process group.
     * Adopt and reap them after killing the group to avoid zombie leaks. */
    for (;;) {
        pid_t reaped = waitpid(-child_pid, NULL, 0);
        if (reaped > 0 || (reaped < 0 && errno == EINTR)) continue;
        break;
    }

    int output_fds[] = {_config->memfd_output, _config->memfd_error};
    for (size_t i = 0; i < sizeof(output_fds) / sizeof(output_fds[0]); i++) {
        if (output_fds[i] < 0) continue;
        if (fstat(output_fds[i], &output_stat) != 0) copy_error = 1;
        else if ((uint64_t)output_stat.st_size > (uint64_t)_config->max_output_size &&
                 _result->result != SYSTEM_ERROR)
            _result->result = OUTPUT_LIMIT_EXCEEDED;
    }
    if (CopyFdToPath(_config->memfd_output, _config->output_path, _config->max_output_size) != 0)
        copy_error = 1;
    if (_config->error_path != NULL &&
        CopyFdToPath(_config->memfd_error, _config->error_path, _config->max_output_size) != 0)
        copy_error = 1;
    if (!copy_error) goto cleanup;
io_failure:
    _result->error = MEMFD_IO_FAILED;
    _result->result = SYSTEM_ERROR;
cleanup:
    RemoveRunCgroup(&cgroup);
    if (_config->memfd_input >= 0) close(_config->memfd_input);
    if (_config->memfd_output >= 0) close(_config->memfd_output);
    if (_config->memfd_error >= 0) close(_config->memfd_error);
    if (status_pipe[0] >= 0) close(status_pipe[0]);
    if (status_pipe[1] >= 0) close(status_pipe[1]);
    if (gate[0] >= 0) close(gate[0]);
    if (gate[1] >= 0) close(gate[1]);
    if (root_created) rmdir(root);
    LogClose(log_fp);
}
