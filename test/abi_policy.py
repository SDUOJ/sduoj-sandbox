#!/usr/bin/env python3
"""Native final-image policy check: python3 test/abi_policy.py aarch64 [sandbox].

Requires root and the original GCC 13.2.0 runtime at /opt/gcc/13.2.0.
Use x86_64 on native AMD64; emulated execution is not acceptance evidence.
Only controlled fixtures are executed, always under an explicit C/C++ policy.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import signal
import subprocess
import tempfile


def main():
    if not __debug__:
        raise SystemExit("Run policy acceptance without Python optimization")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("architecture", choices=("aarch64", "x86_64"))
    parser.add_argument("sandbox", nargs="?", default="/usr/bin/sandbox")
    args = parser.parse_args()
    assert platform.system() == "Linux" and os.geteuid() == 0, "Native Linux root is required"
    assert platform.machine() == args.architecture, "Unexpected runner architecture"
    sandbox = str(Path(args.sandbox).resolve(strict=True))
    sources = Path(__file__).resolve().parent
    gcc = "/opt/gcc/13.2.0/bin/gcc"
    gxx = "/opt/gcc/13.2.0/bin/g++"
    for compiler in (gcc, gxx):
        assert subprocess.check_output([compiler, "-dumpfullversion"], text=True).strip() == "13.2.0"
        assert subprocess.check_output([compiler, "-dumpmachine"], text=True).startswith(args.architecture)
    expected_fields = {"cpu_time", "real_time", "memory", "signal", "exit_code", "error", "result"}
    passed = []
    with tempfile.TemporaryDirectory(prefix="sduoj-policy-") as temporary:
        workspace = Path(temporary)
        workspace.chmod(0o755)
        subprocess.run([gcc, "-std=c99", "-O2", "-static", str(sources / "abi_probe.c"),
                        "-o", str(workspace / "probe")], check=True)
        subprocess.run([gxx, "-std=c++17", "-O2", "-static", str(sources / "abi_sum.cc"),
                        "-o", str(workspace / "sum")], check=True)
        (workspace / "input").write_text("17 25\n")

        def execute(policy, name, executable, extra=()):
            prefix = workspace / (policy + "-" + name)
            command = [sandbox, "--max_cpu_time=1000", "--max_real_time=3000",
                       "--max_memory=268435456", "--max_output_size=65536",
                       "--max_process_number=64", "--uid=65534", "--gid=65534",
                       "--seccomp_rules=" + policy, "--exe_path=" + str(executable),
                       "--input_path=" + str(workspace / "input"),
                       "--output_path=" + str(prefix) + ".out",
                       "--error_path=" + str(prefix) + ".err",
                       "--log_path=" + str(prefix) + ".log"]
            command.extend("--exe_args=" + value for value in extra)
            result = json.loads(subprocess.check_output(command, text=True, timeout=10))
            assert set(result) == expected_fields, result
            assert all(type(value) is int for value in result.values()), result
            stdout = Path(str(prefix) + ".out").read_text()
            stderr = Path(str(prefix) + ".err").read_text()
            return result, stdout, stderr

        for policy in ("c_cpp", "c_cpp_file_io"):
            result, stdout, stderr = execute(policy, "sum", workspace / "sum")
            assert result["result"] == result["signal"] == result["exit_code"] == result["error"] == 0, result
            assert stdout == "42\n" and stderr == "diagnostic\n", (stdout, stderr)
            passed.append({"policy": policy, "case": "static-cpp-stdio", "result": result})
            directory = workspace / policy
            directory.mkdir(mode=0o777)
            directory.chmod(0o777)  # A denied open must not be a filesystem-permission failure.
            if args.architecture == "aarch64":
                result, stdout, stderr = execute(policy, "metadata", workspace / "probe",
                                                ("arm64-metadata", str(directory / "unused")))
                assert result["result"] == result["signal"] == result["exit_code"] == result["error"] == 0, result
                assert stdout == "entered\n" and stderr == "", (stdout, stderr)
                passed.append({"policy": policy, "case": "arm64-metadata", "result": result})
            forbidden = ["socket", "clone", "fork", "openat-write", "openat-readwrite"]
            if policy == "c_cpp":
                forbidden.extend(("open-write", "open-readwrite"))
            # file_io's existing open() grant is intentionally not changed or redefined.
            for name in forbidden:
                target = directory / (name + ".created")
                result, stdout, stderr = execute(policy, name, workspace / "probe", (name, str(target)))
                assert result["signal"] == signal.SIGSYS and result["result"] == 4 and result["error"] == 0, result
                assert stdout == "entered\n" and stderr == "", (stdout, stderr)
                assert not target.exists(), "Forbidden write created a file: " + name
                passed.append({"policy": policy, "case": name, "result": result})
    print(json.dumps({"architecture": args.architecture, "gcc": "13.2.0", "passed": passed}))


if __name__ == "__main__":
    main()
