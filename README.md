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