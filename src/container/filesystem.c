#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "filesystem.h"

static int MakeDirectories(const char *path)
{
    char copy[PATH_MAX];
    if (snprintf(copy, sizeof(copy), "%s", path) >= (int)sizeof(copy))
        return -1;
    for (char *p = copy + 1; *p != '\0'; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(copy, 0755) != 0 && errno != EEXIST) return -1;
        *p = '/';
    }
    return mkdir(copy, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

static int BindPath(const char *root, const char *source, const char *destination, int readonly)
{
    char target[PATH_MAX];
    struct stat st;
    if (stat(source, &st) != 0) return errno == ENOENT ? 0 : -1;
    if (snprintf(target, sizeof(target), "%s%s", root, destination) >= (int)sizeof(target))
        return -1;
    if (S_ISDIR(st.st_mode)) {
        if (MakeDirectories(target) != 0) return -1;
    } else {
        char parent[PATH_MAX];
        snprintf(parent, sizeof(parent), "%s", target);
        char *slash = strrchr(parent, '/');
        if (slash == NULL) return -1;
        *slash = '\0';
        if (MakeDirectories(parent) != 0) return -1;
        struct stat target_st;
        if (lstat(target, &target_st) != 0) {
            int fd = open(target, O_CREAT | O_EXCL | O_CLOEXEC | O_WRONLY, 0600);
            if (fd < 0) return -1;
            close(fd);
        }
    }
    if (mount(source, target, NULL, MS_BIND, NULL) != 0) return -1;

    /* A bind inherits per-mount restrictions from its source.  When a less
     * privileged mount namespace inherits locked flags, omitting one here
     * attempts to relax it and the kernel correctly rejects the remount.
     * Read the actual new bind, preserve its flags, and only add restrictions.
     */
    struct statvfs inherited, applied;
    if (statvfs(target, &inherited) != 0) return -1;
    unsigned long flags = MS_BIND | MS_REMOUNT | MS_NOSUID;
    if (readonly || (inherited.f_flag & ST_RDONLY)) flags |= MS_RDONLY;
    if (inherited.f_flag & ST_NODEV) flags |= MS_NODEV;
    if (inherited.f_flag & ST_NOEXEC) flags |= MS_NOEXEC;
    if (inherited.f_flag & ST_NOATIME) flags |= MS_NOATIME;
    if (inherited.f_flag & ST_NODIRATIME) flags |= MS_NODIRATIME;
    if (inherited.f_flag & ST_RELATIME) flags |= MS_RELATIME;
    if (!(inherited.f_flag & (ST_NOATIME | ST_RELATIME))) flags |= MS_STRICTATIME;
    if (mount(NULL, target, NULL, flags, NULL) != 0) return -1;

    /* Fail closed if the kernel did not retain the requested VFS policy. */
    if (statvfs(target, &applied) != 0) return -1;
    const unsigned long checked = ST_RDONLY | ST_NOSUID | ST_NODEV | ST_NOEXEC |
                                  ST_NOATIME | ST_NODIRATIME | ST_RELATIME;
    unsigned long expected = (inherited.f_flag & checked) | ST_NOSUID;
    if (readonly) expected |= ST_RDONLY;
    if ((applied.f_flag & checked) != expected) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

static int BindArgument(const char *root, const char *cwd, const char *arg)
{
    char path[PATH_MAX];
    struct stat st;
    if (arg[0] == '/') snprintf(path, sizeof(path), "%s", arg);
    else if (snprintf(path, sizeof(path), "%s/%s", cwd, arg) >= (int)sizeof(path)) return -1;
    if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
    return BindPath(root, path, path, 1);
}

static int BindClasses(const char *root, const char *directory, unsigned int depth)
{
    if (depth > 32) return -1;
    DIR *dir = opendir(directory);
    if (dir == NULL) return -1;
    struct dirent *entry;
    int result = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char path[PATH_MAX];
        struct stat st;
        if (snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name) >= (int)sizeof(path)
                || lstat(path, &st) != 0) { result = -1; break; }
        if (S_ISDIR(st.st_mode)) {
            if (BindClasses(root, path, depth + 1) != 0) { result = -1; break; }
        } else if (S_ISREG(st.st_mode)) {
            const char *ext = strrchr(entry->d_name, '.');
            if (ext != NULL && (strcmp(ext, ".class") == 0 || strcmp(ext, ".jar") == 0)) {
                if (BindPath(root, path, path, 1) != 0) { result = -1; break; }
            }
        }
    }
    closedir(dir);
    return result;
}

int SetupFilesystem(struct config *config, const char *root)
{
    char cwd[PATH_MAX], path[PATH_MAX], executable[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL || realpath(config->exe_path, executable) == NULL)
        return -1;
    if (unshare(CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWNET) != 0
            || mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0
            || mount("tmpfs", root, "tmpfs", MS_NOSUID | MS_NODEV, "size=16m,mode=0755") != 0)
        return -1;

    /* Runtime mounts are immutable.  None contains checkpoints or judger
     * workspaces.  Bind non-recursively so nested host mounts are excluded. */
    const char *runtime[] = {"/usr", "/bin", "/sbin", "/lib", "/lib32", "/lib64", "/etc"};
    for (size_t i = 0; i < sizeof(runtime) / sizeof(runtime[0]); i++)
        if (BindPath(root, runtime[i], runtime[i], 1) != 0) return -1;
    if (strncmp(executable, "/opt/", 5) == 0 && strstr(executable, "/bin/") != NULL) {
        char toolchain[PATH_MAX];
        snprintf(toolchain, sizeof(toolchain), "%s", executable);
        char *slash = strchr(toolchain + 5, '/');
        if (slash == NULL) return -1;
        *slash = '\0';
        if (BindPath(root, toolchain, toolchain, 1) != 0) return -1;
    }
    snprintf(path, sizeof(path), "%s/tmp", root);
    if (MakeDirectories(path) != 0
            || mount("tmpfs", path, "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC,
                     "size=64m,mode=1777") != 0) return -1;

    /* The child is created as PID 1 in a private PID namespace.  Mount a
     * procfs for that namespace instead of binding the host's /proc/<pid>;
     * this keeps descendant enumeration and signal semantics local to the
     * submission. */
    snprintf(path, sizeof(path), "%s/proc", root);
    if (MakeDirectories(path) != 0 ||
        mount("proc", path, "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RDONLY,
              "hidepid=2") != 0)
        return -1;

    const char *devices[] = {"/dev/null", "/dev/zero", "/dev/random", "/dev/urandom"};
    for (size_t i = 0; i < sizeof(devices) / sizeof(devices[0]); i++)
        if (BindPath(root, devices[i], devices[i], i != 0) != 0) return -1;
    const char *procfiles[] = {"/proc/cpuinfo", "/proc/meminfo", "/proc/stat", "/proc/uptime",
                              "/proc/loadavg", "/proc/sys/kernel/osrelease"};
    for (size_t i = 0; i < sizeof(procfiles) / sizeof(procfiles[0]); i++)
        if (BindPath(root, procfiles[i], procfiles[i], 1) != 0) return -1;
    if (snprintf(path, sizeof(path), "%s%s", root, cwd) >= (int)sizeof(path)
            || MakeDirectories(path) != 0) return -1;
    if (!config->work_readonly) {
        if (BindPath(root, cwd, cwd, 0) != 0) return -1;
    } else {
        const char *name = strrchr(executable, '/');
        if (name != NULL && strcmp(name + 1, "java") == 0 && BindClasses(root, cwd, 0) != 0)
            return -1;
    }
    if (BindPath(root, executable, executable, 1) != 0) return -1;
    /* These arguments come from the trusted template, not the submission.
     * Checkers may receive input/output/answer paths outside their cwd. */
    for (int i = 1; i <= MAX_ARG && config->exe_args[i] != NULL; i++)
        if (BindArgument(root, cwd, config->exe_args[i]) != 0) return -1;
    if (mount(NULL, root, NULL, MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV, NULL) != 0
            || chroot(root) != 0 || chdir(cwd) != 0) return -1;
    return 0;
}
