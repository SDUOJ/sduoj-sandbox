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
    --seccomp_rules=general \              # systemcall filter rules, optional `c_cpp`, `c_cpp_file_io`
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

After the program finishes, it outputs a JSON line describing the result and resource usage.

## Maintainers

[@jesHrz](https://github.com/jesHrz)

[@Gene](https://github.com/GeneLiuXe)

## Credits

We thank the following projects for inspiration and technical support:

- [QOJ-Judger](https://github.com/QingdaoU/Judger)
- [How-it-works](https://docs.onlinejudge.me/#/judger/how_it_works)

## License

[SATA](https://github.com/SDUOJ/sduoj-sandbox/blob/master/LICENSE)

