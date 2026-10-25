# SDUOJ-SANDBOX

This is the sandbox of judger for SDUOJ, which provides a secure environment for running user's code that is submitted from web.

The sandbox can run on `Linux` with system call `setrlimit` and `seccomp` to limit the resource and some dangerous system calls.

# Install

Building this sandbox need `libseccomp-dev`, you can install it with command followed.

```
$ sudo apt-get install libseccomp-dev
```

Then, you can install the sandbox to `/opt/sduoj-sandbox/bin/sandbox` and `/usr/bin/sandbox`

```
$ make
$ sudo make install
```

If you want to uninstall it, you can run 

```
$ sudo make uninstall
```

# Usage

### Example

You can run the sandbox with such command

```
# root privilege is needed
$ sudo ./sandbox \
    --io_mode=memfd --isolate_fs=1 --work_readonly=1 \
    --exe_path="/usr/bin/python3" --exe_args="test/test_py.py" \
    --input_path="test/input.txt" --output_path="test/output.txt" \
    --seccomp_rules=general --max_cpu_time=1000 --max_real_time=2000 \
    --max_memory=33554432 --max_stack=134217728 \
    --max_process_number=128 --max_output_size=1048576
```

Standard I/O always uses sealed memfd; `disk` is rejected. Filesystem
isolation is required, and candidate work directories are read-only. Use
`--help` for the full option list.

Each run requires a writable delegated cgroup v2 directory with the memory
and pids controllers enabled. Set `SDUOJ_CGROUP_ROOT` or pass
`--cgroup_root=/path/to/delegated/runs`. The sandbox writes `memory.max`,
`memory.swap.max=0`, `memory.oom.group=1` and `pids.max` before releasing its
launch gate. `pids.max` includes the trusted namespace init. Missing cgroup
or namespace support returns a system error; it never falls back to disk.

Docker workers use `docker/judge-entrypoint.sh` with `SYS_ADMIN` and a private
cgroup namespace (`--cap-add SYS_ADMIN --cgroupns=private`). On AppArmor hosts,
the Docker profile must permit the private mounts; the validated Compose
configuration uses `apparmor:unconfined` for this outer worker container.
The submitted process still receives Landlock, seccomp and zero capabilities.
The entrypoint
mounts only the container's cgroup subtree and creates separate worker/run
branches. Do not mount the host cgroup hierarchy into the worker.

The submitted program runs as PID 2 in its own PID namespace. A trusted PID 1
waits for it, relays its exit status and `wait4` usage, and kills/reaps remaining
descendants. CPU and RSS for ordinary execution and watchdog termination keep
the per-program rusage metric. If a cgroup OOM also kills PID 1, aggregate cgroup
CPU and memory peak provide failure diagnostics instead. These memory metrics
have different scopes and should not be combined as performance samples.

# Maintainers

[@jesHrz](https://github.com/jesHrz)

[@Gene](https://github.com/GeneLiuXe)

# Credits

I would like to extend my sincere gratitude to those projects listed below for providing us solid technical support and creative inspiration.

- [QOJ-Judger](https://github.com/QingdaoU/Judger)

- [How-it-works](https://docs.onlinejudge.me/#/judger/how_it_works)

# License

[SATA](https://github.com/SDUOJ/sduoj-sandbox/blob/master/LICENSE)
