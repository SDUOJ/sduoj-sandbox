#define _GNU_SOURCE
/* Linux regression: gcc -std=c99 -Wall -Wextra tests/bind_mount_flags.c -o /tmp/bind-mount-flags
 * Run as the trusted integration-test administrator with mount and mknod rights.
 * The tested helper runs after a new user namespace locks inherited mount flags.
 */
#include <assert.h>
#include <sys/wait.h>
#include <sys/sysmacros.h>
#include <sys/statvfs.h>
#include "../src/container/filesystem.c"

static void WriteFile(const char *path, const char *value, mode_t mode)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    assert(fd >= 0);
    assert(write(fd, value, strlen(value)) == (ssize_t)strlen(value));
    assert(close(fd) == 0);
}

static void MapIdentity(const char *file, unsigned int id)
{
    char mapping[64];
    snprintf(mapping, sizeof(mapping), "0 %u 1\n", id);
    WriteFile(file, mapping, 0600);
}

static void AssertInNamespace(const char *root, const char *source, int readonly)
{
    uid_t uid = getuid();
    gid_t gid = getgid();
    assert(unshare(CLONE_NEWUSER | CLONE_NEWNS) == 0);
    WriteFile("/proc/self/setgroups", "deny", 0600);
    MapIdentity("/proc/self/uid_map", uid);
    MapIdentity("/proc/self/gid_map", gid);

    struct statvfs before, after;
    assert(statvfs(source, &before) == 0);
    assert(BindPath(root, source, "/view", readonly) == 0);
    char target[PATH_MAX], path[PATH_MAX];
    snprintf(target, sizeof(target), "%s/view", root);
    assert(statvfs(target, &after) == 0);
    const unsigned long mask = ST_RDONLY | ST_NOSUID | ST_NODEV | ST_NOEXEC |
                               ST_NOATIME | ST_NODIRATIME | ST_RELATIME;
    unsigned long expected = (before.f_flag & mask) | ST_NOSUID;
    if (readonly) expected |= ST_RDONLY;
    assert((after.f_flag & mask) == expected);

    snprintf(path, sizeof(path), "%s/write", target);
    int fd = open(path, O_WRONLY | O_CREAT, 0600);
    if (expected & ST_RDONLY) {
        assert(fd == -1 && errno == EROFS);
    } else {
        assert(fd >= 0);
        assert(close(fd) == 0);
    }
    snprintf(path, sizeof(path), "%s/null", target);
    fd = open(path, O_RDONLY);
    assert(fd == -1 && errno == EACCES); /* nodev is still enforced */

    snprintf(path, sizeof(path), "%s/run", target);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        char *argv[] = {path, NULL};
        execve(path, argv, environ);
        _exit(errno == EACCES ? 0 : 1);
    }
    int status;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0); /* noexec */
}

int main(void)
{
    const struct { const char *name; unsigned long atime; int source_ro; int readonly; } cases[] = {
        {"relatime", MS_RELATIME, 0, 0},
        {"source-readonly", MS_RELATIME, 1, 0},
        {"noatime-nodiratime", MS_NOATIME | MS_NODIRATIME, 0, 0},
        {"strictatime", MS_STRICTATIME, 0, 0},
        {"add-readonly", MS_RELATIME, 0, 1},
    };
    assert(geteuid() == 0);
    assert(unshare(CLONE_NEWNS) == 0);
    assert(mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == 0);
    char base[] = "/tmp/sduoj-bind-flags-XXXXXX";
    assert(mkdtemp(base) != NULL);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char source[PATH_MAX], root[PATH_MAX], path[PATH_MAX];
        snprintf(source, sizeof(source), "%s/source", base);
        snprintf(root, sizeof(root), "%s/root", base);
        assert(mkdir(source, 0755) == 0 && mkdir(root, 0755) == 0);
        unsigned long flags = MS_NOSUID | MS_NODEV | MS_NOEXEC | cases[i].atime;
        assert(mount("tmpfs", source, "tmpfs", flags, "size=1m,mode=0755") == 0);
        snprintf(path, sizeof(path), "%s/run", source);
        WriteFile(path, "#!/bin/sh\nexit 42\n", 0755);
        snprintf(path, sizeof(path), "%s/null", source);
        assert(mknod(path, S_IFCHR | 0666, makedev(1, 3)) == 0);
        if (cases[i].source_ro)
            assert(mount(NULL, source, NULL, MS_REMOUNT | MS_RDONLY | flags, NULL) == 0);
        pid_t pid = fork();
        assert(pid >= 0);
        if (pid == 0) {
            AssertInNamespace(root, source, cases[i].readonly);
            _exit(0);
        }
        int status;
        assert(waitpid(pid, &status, 0) == pid);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        printf("PASS bind flags %s\n", cases[i].name);
        fflush(stdout);
        assert(umount(source) == 0);
        snprintf(path, sizeof(path), "%s/view", root);
        assert(rmdir(path) == 0 && rmdir(root) == 0 && rmdir(source) == 0);
    }
    assert(rmdir(base) == 0);
    return 0;
}
