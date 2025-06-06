# SDUOJ-SANDBOX

This project provides the sandbox used by SDU Online Judge. It creates a restricted environment for running user submitted programs using `setrlimit` and `seccomp`.

## Install

The sandbox depends on `libseccomp-dev`.

```bash
sudo apt-get install libseccomp-dev
```

Compile and install to `/opt/sduoj-sandbox/bin/sandbox` and `/usr/bin/sandbox`:

```bash
make
sudo make install
```

Uninstall with:

```bash
sudo make uninstall
```

## Usage

### Example

```bash
# root privilege is needed
sudo ./sandbox \
    --exe_path "/usr/bin/python3" \        # executable file path
    --exe_args "test/test_py.py"  \        # arguments for the executable
    --input_path "test/input.txt" \        # where to get input
    --output_path "test/output.txt" \      # where the output should be
    --seccomp_rules=general \              # seccomp preset: `c_cpp`, `c_cpp_file_io`, or `general`
    --max_memory=33554432                  # maximum memory in bytes
```

Use `--help` to get a concise overview of all options.

### Command-Line Options

| Option | Description | Default |
|-------|-------------|---------|
| `--max_cpu_time <n>` | CPU time limit in milliseconds | unlimited |
| `--max_real_time <n>` | Wall clock time limit in milliseconds | unlimited |
| `--max_memory <str>` | Virtual memory limit in bytes | unlimited |
| `--max_stack <str>` | Stack size limit in bytes | 16 MB |
| `--max_process_number <n>` | Maximum number of processes | unlimited |
| `--max_output_size <n>` | Maximum output file size in bytes | unlimited |
| `--exe_path <str>` | **Required** path to the executable | – |
| `--input_path <str>` | Input file path | `/dev/stdin` |
| `--output_path <str>` | Output file path | `/dev/stdout` |
| `--log_path <str>` | Log file path | `sandbox.log` |
| `--exe_args <str>` | Arguments for the executable (repeatable) | none |
| `--exe_envs <str>` | Environment variables (repeatable) | none |
| `--seccomp_rules <str>` | Seccomp filter (`c_cpp`, `c_cpp_file_io`, `general`) | none |
| `--uid <n>` | UID for running the executable (default `nobody`) | system nobody |
| `--gid <n>` | GID for running the executable (default `nobody`) | system nobody |
| `--help` | Show usage information | – |
| `--version` | Show program version | – |

### Preset Seccomp Rules

The `--seccomp_rules` option controls which system calls are allowed while the
program is running. Three preset policies are available:

| Name | Behaviour | Typical use |
|------|-----------|-------------|
| `c_cpp` | Whitelist allowing only common computation related syscalls. Opening
files for writing is blocked. | Compiled C/C++ programs without file output |
| `c_cpp_file_io` | Same as `c_cpp` but file writes and descriptor duplication
(`dup*`) are permitted. | C/C++ programs that need to create or write files |
| `general` | Blacklist based policy intended for interpreters. It kills
processes that attempt to fork and denies opening files in write mode. The
`socket` syscall returns `EACCES`. | Scripting languages or other dynamic
environments |

After the program finishes, it outputs a JSON line describing the result and resource usage.

### Result JSON Format

After execution the sandbox prints a single JSON object describing how the
program ran. A typical output looks like:

```json
{"cpu_time":12,"real_time":15,"memory":204800,"signal":0,"exit_code":0,"error":0,"result":0}
```

The object contains the following fields:

| Field | Meaning |
|-------|---------|
| `cpu_time` | User CPU time consumed (ms) |
| `real_time` | Wall clock time from start to finish (ms) |
| `memory` | Peak memory usage in bytes |
| `signal` | Signal that terminated the process, or `0` if none |
| `exit_code` | Exit status of the program |
| `error` | Sandbox error code |
| `result` | Final judgement result code |

#### Error Code Values

| Value | Name | Description |
|----|---------------|--------------------------------|
| 0  | `SUCCESS` | everything is ok |
| 1  | `INVALID_CONFIG` | invalid config |
| 2  | `FORK_FAILED` | `fork()` failed |
| 3  | `PTHREAD_FAILED` | thread creation failed |
| 4  | `WAIT_FAILED` | `wait4()` failed |
| 5  | `DUP2_FAILED` | `dup2()` failed |
| 6  | `SETRLIMIT_FAILED` | `setrlimit()` failed |
| 7  | `SETUID_FAILED` | `setuid()` or `setgid()` failed |
| 8  | `LOAD_SECCOMP_FAILED` | loading seccomp rules failed |
| 9  | `EXECVE_FAILED` | `execve()` failed |
| 10 | `SPJ_ERROR` | Special Judge failed |
| 11 | `ROOT_REQUIRED` | sandbox must run as root |
| 12 | `NOBODY_REQUIRED` | user program must run as nobody |

#### Result Code Values

| Value | Name | Description |
|----|---------------------------|--------------------------------|
| 0 | `SUCCESS` | program finished normally |
| 1 | `CPU_TIME_LIMIT_EXCEEDED` | exceeded CPU time limit |
| 2 | `REAL_TIME_LIMIT_EXCEEDED` | exceeded real time limit |
| 3 | `MEMORY_LIMIT_EXCEEDED` | exceeded memory limit |
| 4 | `RUNTIME_ERROR` | runtime error or killed by signal |
| 5 | `SYSTEM_ERROR` | sandbox system error |
| 6 | `OUTPUT_LIMIT_EXCEEDED` | exceeded output size limit |

## Maintainers

[@jesHrz](https://github.com/jesHrz)

[@Gene](https://github.com/GeneLiuXe)

## Credits

We thank the following projects for inspiration and technical support:

- [QOJ-Judger](https://github.com/QingdaoU/Judger)
- [How-it-works](https://docs.onlinejudge.me/#/judger/how_it_works)

## License

[SATA](https://github.com/SDUOJ/sduoj-sandbox/blob/master/LICENSE)

