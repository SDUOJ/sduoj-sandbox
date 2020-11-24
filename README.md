# SDUOJ-SANDBOX

This is the sandbox of judger for SDUOJ, which provides a secure environment for running user's code that is submitted from web.

The sandbox can run on `Linux` with system call `setrlimit` and `seccomp` to limit the resource and some dangerous system calls.

# Install

For this sandbox use `seccomp`, you need to add it to your system. And you can install seccomp with command followed.

```
$ sudo apt-get install libseccomp-dev libseccomp2 seccomp
```

Then, you can run

```
$ make
$ sudo make install
```

to generate the sandbox. The sandbox will be linked to `/usr/bin/`. If you want to uninstall it, you can run 

```
$ sudo make clean
```

# Usage

### Example

You can run the sandbox with such command

```
# root privilege is needed
$ sudo ./sandbox \
    --exe_path="/usr/bin/python3" \     # executable file path
    --exe_args="test/test_py.py"  \     # args of executable file
    --input_path="test/input.txt" \     # where to get input
    --output_path="test/output.txt" \   # where the output should be
    --seccomp_rules=general \           # systemcall filte rules, optional `c_cpp`, `c_cpp_file_io`
    --max_memory=33554432               # maximum memnory in bytes
```

Use `--help` to get more information about usages.

# Maintainers

[@jesHrz](https://github.com/jesHrz)

[@Gene](https://github.com/GeneLiuXe)

# Credits

I would like to extend my sincere gratitude to those projects listed below for providing us solid technical support and creative inspiration.

- [QOJ-Judger](https://github.com/QingdaoU/Judger)

- [How-it-works](https://docs.onlinejudge.me/#/judger/how_it_works)

# License

[SATA](https://github.com/SDUOJ/sduoj-sandbox/blob/master/LICENSE)