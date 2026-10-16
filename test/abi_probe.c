#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

/* A marker proves that each forbidden call is reached after libc startup. */
int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    if (write(STDOUT_FILENO, "entered\n", 8) != 8) return 3;
    if (!strcmp(argv[1], "socket")) {
        syscall(SYS_socket, AF_INET, SOCK_STREAM, 0);
    } else if (!strcmp(argv[1], "clone")) {
        syscall(SYS_clone, SIGCHLD, 0, 0, 0, 0);
    } else if (!strcmp(argv[1], "fork")) {
        fork();
    } else if (!strcmp(argv[1], "open-write")) {
        open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0600);
    } else if (!strcmp(argv[1], "open-readwrite")) {
        open(argv[2], O_RDWR | O_CREAT | O_TRUNC, 0600);
    } else if (!strcmp(argv[1], "openat-write")) {
        syscall(SYS_openat, AT_FDCWD, argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0600);
    } else if (!strcmp(argv[1], "openat-readwrite")) {
        syscall(SYS_openat, AT_FDCWD, argv[2], O_RDWR | O_CREAT | O_TRUNC, 0600);
#if defined(__aarch64__)
    } else if (!strcmp(argv[1], "arm64-metadata")) {
        char target[4096];
        if (syscall(SYS_readlinkat, AT_FDCWD, "/proc/self/exe", target, sizeof(target)) <= 0) return 4;
        return syscall(SYS_faccessat, AT_FDCWD, "/proc/self/exe", R_OK) == 0 ? 0 : 5;
#endif
    } else {
        return 6;
    }
    /* Even a denied errno is a failure: the policy must terminate the call. */
    return 77;
}
