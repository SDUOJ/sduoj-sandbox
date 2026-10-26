"""Run inside docker/test.Dockerfile's Linux image as root with CAP_SYS_ADMIN.

python3 tests/integration.py --java-class /fixtures/Main.class
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


parser = argparse.ArgumentParser()
parser.add_argument("--java-class", type=Path, required=True)
parser.add_argument("--results", type=Path)
args = parser.parse_args()
sandbox = Path(os.environ.get("TEST_SANDBOX", Path(__file__).resolve().parents[1] / "build/bin/sandbox"))
base = Path(tempfile.mkdtemp(prefix="judge-hardening-", dir="/var/tmp"))
base.chmod(0o755)
data = Path("/data")
data.mkdir(exist_ok=True)
(data / "answer").write_text("private-answer-sentinel\n")
input_file = base / "input"
input_file.write_text("41\n")
records = []


def run(name, source=None, executable="/usr/bin/python3", argv=None, expected=0,
        cpu=1000, wall=3000, memory=536870912, rule="general", stdin=input_file,
        output_limit=1048576, readonly=True, files=None, environment=None, pass_fds=(), extra=None):
    cwd = base / name
    cwd.mkdir(mode=0o755, exist_ok=True)
    cwd.chmod(0o755 if readonly else 0o777)
    for filename, content in (files or {}).items():
        (cwd / filename).write_text(content)
    if source is not None:
        (cwd / "main.py").write_text(source)
        argv = ["main.py"]
    if executable == "java":
        shutil.copyfile(args.java_class, cwd / "Main.class")
        executable = os.environ.get("TEST_JAVA", "/usr/bin/java")
        argv = ["-Xmx128m", "-XX:CompressedClassSpaceSize=64m", "-XX:ReservedCodeCacheSize=64m",
                "-XX:+UseSerialGC", "-cp", ".", "Main"]
    if executable == "c":
        (cwd / "main.c").write_text("#include <stdio.h>\nint main(){int x;if(scanf(\"%d\",&x)!=1)return 2;printf(\"%d\\n\",x+1);}\n")
        subprocess.run(["gcc", "-O2", "main.c", "-o", "main"], cwd=cwd, check=True)
        executable = str(cwd / "main")
        argv = []
    out, err, log = (base / (name + suffix) for suffix in (".out", ".err", ".log"))
    command = [str(sandbox), "--io_mode=memfd", "--isolate_fs=1", "--work_readonly=" + str(int(readonly)),
               "--uid=65534", "--gid=65534", "--max_process_number=128",
               "--max_cpu_time=" + str(cpu), "--max_real_time=" + str(wall),
               "--max_memory=" + str(memory), "--max_stack=134217728",
               "--max_output_size=" + str(output_limit), "--max_io_input_size=1048576",
               "--cgroup_root=" + os.environ.get("SDUOJ_CGROUP_ROOT", "/tmp/cg"),
               "--exe_path=" + executable, "--input_path=" + str(stdin),
               "--output_path=" + str(out), "--error_path=" + str(err),
               "--log_path=" + str(log)]
    if rule:
        command.append("--seccomp_rules=" + rule)
    command += ["--exe_args=" + value for value in (argv or [])]
    command += ["--exe_envs=" + value for value in (environment or [])]
    for option in extra or []:
        prefix = option.split('=', 1)[0] + '='
        command = [value for value in command if not value.startswith(prefix)]
        command.append(option)
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, pass_fds=pass_fds, timeout=wall / 1000 + 15)
    receipt = {"name": name, "sandbox": json.loads(result.stdout),
               "stdout": out.read_text(errors="replace")[:128] if out.exists() else "",
               "stderr": err.read_text(errors="replace")[:128] if err.exists() else "",
               "error_bytes": err.stat().st_size if err.exists() else 0,
               "output_bytes": out.stat().st_size if out.exists() else 0,
               "log": log.read_text() if log.exists() else ""}
    records.append(receipt)
    print(json.dumps(receipt), flush=True)
    assert result.returncode == 0, result.stderr
    assert receipt["sandbox"]["result"] == expected, receipt
    return receipt


try:
    result = run("python", """import os,sys,socket
assert os.listdir('.') == ['main.py'], os.listdir('.')
assert sys.stdin.read() == '41\\n'
assert os.readlink('/proc/self/fd/0').startswith('/memfd:sduoj-input')
for p in ['/data/answer', '../input', '/proc/1/environ', '/proc/1/root/data/answer',
          '/proc/self/root/data/answer']:
    try:
        open(p).read()
    except OSError:
        pass
    else:
        raise AssertionError(p)
try:
    socket.socket()
except OSError:
    pass
else:
    raise AssertionError('network')
try:
    os.fork()
except OSError:
    pass
else:
    raise AssertionError('fork')
print('python-isolation-ok')
""")
    assert result['stdout'] == 'python-isolation-ok\n', result
    result = run("native", executable="c", rule="c_cpp")
    assert result['stdout'] == '42\n', result
    result = run("java", executable="java", cpu=3000, wall=8000)
    assert result['stdout'] == '41\nanswer:FileNotFoundException\n', result
    result = run("empty", "import sys; assert sys.stdin.read() == ''; print('empty-ok')\n", stdin="/dev/null")
    assert result['stdout'] == 'empty-ok\n', result
    result = run("output", "import sys; sys.stdout.write('x' * 1048577)\n", expected=6)
    assert result['output_bytes'] == 1048576, result
    result = run("wall", "import time; time.sleep(2)\n", cpu=1000, wall=100, expected=2)
    assert result['sandbox']['memory'] > 1024 * 1024, result
    result = run("cpu", "while True: pass\n", cpu=50, wall=1500, expected=1)
    assert 45 <= result['sandbox']['cpu_time'] <= 70, result
    assert result['sandbox']['memory'] > 1024 * 1024, result
    run("output_exact", "import os; os.write(1, b'x' * 1048576)\n")
    result = run("large_answer_limit", "import os; os.write(1, b'x' * 1400001)\n",
                 output_limit=1400000, expected=6)
    assert result['output_bytes'] == 1400000
    result = run("stderr_limit", "import os; os.write(2, b'x' * 1048577)\n", expected=6)
    assert result['error_bytes'] == 1048576
    run("memory", "import time; data = bytearray(36 * 1024 * 1024); time.sleep(2)\n",
        memory=32 * 1024 * 1024, expected=3)
    result = run("virtual_reservation", """import mmap
region = mmap.mmap(-1, 1024 * 1024 * 1024)
region[0] = 1
region[-1] = 1
print('reserved-not-resident')
""", memory=64 * 1024 * 1024)
    assert result['stdout'] == 'reserved-not-resident\n', result
    assert result['sandbox']['memory'] < 64 * 1024 * 1024, result
    run("sealed_input", """import os
for operation in [lambda: os.write(0,b'x'), lambda: os.ftruncate(0,0)]:
    try: operation()
    except OSError: pass
    else: raise AssertionError('input not sealed')
assert os.read(0,10) == b'41\\n'
print('sealed')
""")
    with open(data / 'answer') as inherited:
        fd = inherited.fileno()
        run("inherited_fd", f"""import os
try: os.fstat({fd})
except OSError: print('closed')
else: raise AssertionError('descriptor leaked')
""", pass_fds=(fd,))
    result = run("compile", executable="/usr/bin/gcc", argv=["main.c", "-o", "main"],
                 files={"main.c": '#include <stdio.h>\nint main(){puts("compiled");}\n'},
                 readonly=False, rule=None, cpu=3000, wall=6000,
                 environment=['PATH=/usr/bin:/bin', 'TMPDIR=/tmp'])
    result = run("compile", executable=str(base / 'compile/main'), argv=[], rule='c_cpp')
    assert result['stdout'] == 'compiled\n'
    checker_source = base / 'checker.c'
    checker_source.write_text('#include <stdio.h>\nint main(int n,char **v){char s[128]; FILE *f=fopen(v[1],"r");if(!f)return 2;fgets(s,128,f);puts(s);return 0;}\n')
    subprocess.run(['gcc', str(checker_source), '-o', str(base / 'checker-bin')], check=True)
    result = run("checker", executable=str(base / 'checker-bin'), argv=[str(data / 'answer')],
                 readonly=False, rule=None)
    assert 'private-answer-sentinel' in result['stdout']
    run("disk_rejected", "print('must-not-run')\n", extra=['--io_mode=disk'], expected=5)
    run("file_io_rejected", "print('must-not-run')\n", rule='c_cpp_file_io', expected=5)
    run("exec_failure", executable='/does-not-exist', expected=5)
    run("signal_not_system", "import os,signal; os.kill(os.getpid(), signal.SIGUSR1)\n", expected=4)
    run("raw_syscalls", """import ctypes,errno,os,platform,threading
libc=ctypes.CDLL(None,use_errno=True)
numbers={'aarch64': (220,435,425,279,270,279), 'x86_64': (56,435,425,319,310,319)}
clone,clone3,uring,memfd,vmread,_=numbers[platform.machine()]
for call, values, error in [(clone,[17,0,0,0,0],errno.EPERM),
    (clone,[0x100|0x10000,0,0,0,0],errno.EPERM), (clone3,[0,0],errno.ENOSYS),
    (uring,[1,0],errno.EPERM),(memfd,[0,0],errno.EPERM),
    (vmread,[os.getppid(),0,0,0,0,0],errno.EPERM)]:
    assert libc.syscall(call,*values) == -1
    assert ctypes.get_errno() == error, (call,ctypes.get_errno())
for call in (424,426,427,428,429,430,431,432,433,434,438,440,442,448):
    assert libc.syscall(call,0,0,0,0,0,0)==-1
    assert ctypes.get_errno()==errno.EPERM,(call,ctypes.get_errno())
threads=[threading.Thread(target=lambda: sum(range(10000))) for _ in range(4)]
for t in threads: t.start()
for t in threads: t.join()
print('threads-yes-processes-no')
""")
    run("socketpair_safety", r"""import ctypes,errno,os,socket,array
libc=ctypes.CDLL(None,use_errno=True)
for domain,kind,protocol in [(socket.AF_INET,socket.SOCK_STREAM,0),(socket.AF_UNIX,socket.SOCK_DGRAM,0),(socket.AF_UNIX,socket.SOCK_SEQPACKET|socket.SOCK_CLOEXEC,0),(socket.AF_UNIX,socket.SOCK_STREAM,1),(socket.AF_UNIX,socket.SOCK_STREAM|socket.SOCK_CLOEXEC,0)]:
 fds=(ctypes.c_int*2)();ctypes.set_errno(0)
 assert libc.socketpair(domain,kind,protocol,fds)==-1
 assert ctypes.get_errno()==errno.EACCES,(domain,kind,protocol,ctypes.get_errno())
for domain in (socket.AF_UNIX,socket.AF_INET,socket.AF_INET6):
 ctypes.set_errno(0);assert libc.socket(domain,socket.SOCK_STREAM,0)==-1
 assert ctypes.get_errno()==errno.EACCES
ctypes.set_errno(0);assert libc.recvfrom(-1,0,0,0,0,0)==-1
assert ctypes.get_errno()==errno.EACCES
for method in ('connect','bind'):
 ctypes.set_errno(0);assert getattr(libc,method)(-1,ctypes.c_char_p(b'\x01\x00/tmp/sduoj-must-not-connect'),32)==-1
 assert ctypes.get_errno()==errno.EACCES,(method,ctypes.get_errno())
fds=(ctypes.c_int*2)();assert libc.socketpair(socket.AF_UNIX,socket.SOCK_STREAM,0,fds)==0
os.write(fds[0],b'local');assert os.read(fds[1],5)==b'local'
a=socket.socket(fileno=fds[0]);b=socket.socket(fileno=fds[1])
try:a.sendmsg([b'x'],[(socket.SOL_SOCKET,socket.SCM_RIGHTS,array.array('i',[1]))])
except OSError as error:assert error.errno==errno.EACCES,error
else:raise AssertionError('descriptor passing escaped')
try:b.recvmsg(1,128)
except OSError as error:assert error.errno==errno.EACCES,error
else:raise AssertionError('recvmsg escaped')
a.close();b.close();print('exact anonymous pair works; external sockets, named paths, fd transfer denied')
""")
    run("compiler_socket_handshake", r"""import ctypes,errno,os,socket
libc=ctypes.CDLL(None,use_errno=True)
fds=(ctypes.c_int*2)();assert libc.socketpair(socket.AF_UNIX,socket.SOCK_SEQPACKET|socket.SOCK_CLOEXEC,0,fds)==0
for flags,addr,addrlen in [(1,0,0),(0,1,0),(0,0,1)]:
    ctypes.set_errno(0);assert libc.recvfrom(fds[0],0,0,flags,addr,addrlen)==-1
    assert ctypes.get_errno()==errno.EACCES
os.write(fds[1],b'handshake');buf=ctypes.create_string_buffer(32)
assert libc.recvfrom(fds[0],buf,32,0,0,0)==9
assert buf.value==b'handshake'
for call in ('sendmsg','recvmsg'):
    ctypes.set_errno(0);assert getattr(libc,call)(fds[0],0,0)==-1
    assert ctypes.get_errno()==errno.EACCES
os.close(fds[0]);os.close(fds[1]);print('compiler-handshake-only')
""", readonly=False, rule=None)
    rustc = Path('/opt/rust/1.78.0/bin/rustc')
    if rustc.is_file():
        run("rust_compile", executable=str(rustc),
            argv=['-C', 'opt-level=2', '--cfg', 'ONLINE_JUDGE', '-o', 'main', 'main.rs'],
            files={'main.rs': 'fn main(){println!("rust-compiled");}\n'},
            readonly=False, rule=None, cpu=3000, wall=5000, memory=268435456,
            output_limit=16 * 1024 * 1024,
            environment=['PATH=/usr/bin:/bin'])
        result = run("rust_runtime", executable=str(base / 'rust_compile/main'), rule='general')
        assert result['stdout'] == 'rust-compiled\n'
    calibration = base / 'calibration.c'
    calibration.write_text(r"""#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/resource.h>
static long long ns(void){struct timespec t; clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t); return t.tv_sec*1000000000LL+t.tv_nsec;}
int main(void){long long start=ns(); volatile char *p=malloc(24*1024*1024); if(!p)return 2;
for(int i=0;i<24*1024*1024;i+=4096)p[i]=1;
while(ns()-start<200000000){} struct rusage r;getrusage(RUSAGE_SELF,&r);
printf("%lld %ld\n",(ns()-start)/1000000,r.ru_maxrss*1024);return 0;}
""")
    subprocess.run(['gcc','-O2',str(calibration),'-o',str(base/'calibration-bin')],check=True)
    result=run('calibration',executable=str(base/'calibration-bin'),rule='c_cpp')
    measured_cpu,measured_rss=map(int,result['stdout'].split())
    assert abs(result['sandbox']['cpu_time']-measured_cpu) <= 10, result
    assert abs(result['sandbox']['memory']-measured_rss) <= 1024*1024, result
    print(f"PASS: {len(records)} integration cases", flush=True)
finally:
    if args.results:
        args.results.write_text(json.dumps(records, ensure_ascii=False, indent=2) + "\n")
    shutil.rmtree(base)
