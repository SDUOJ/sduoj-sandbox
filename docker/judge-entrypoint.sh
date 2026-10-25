#!/bin/sh
set -eu

# A private cgroup namespace makes this mount refer only to this container's
# delegated subtree. Never bind the host cgroup hierarchy into the worker.
if [ "$(cat /proc/self/cgroup)" != "0::/" ]; then
    echo "A private cgroup v2 namespace is required" >&2
    exit 1
fi
SDUOJ_CGROUP_MOUNT=/run/sduoj-cgroup
export SDUOJ_CGROUP_ROOT="$SDUOJ_CGROUP_MOUNT/runs"
mkdir -p "$SDUOJ_CGROUP_MOUNT"
mount -t cgroup2 -o nosuid,nodev,noexec none "$SDUOJ_CGROUP_MOUNT"
mkdir "$SDUOJ_CGROUP_MOUNT/worker"
# Move tini and the entrypoint into a leaf before enabling domain controllers.
for sduoj_pid in $(cat "$SDUOJ_CGROUP_MOUNT/cgroup.procs"); do
    if ! { echo "$sduoj_pid" > "$SDUOJ_CGROUP_MOUNT/worker/cgroup.procs"; } 2>/dev/null; then
        [ ! -d "/proc/$sduoj_pid" ] || exit 1
    fi
done
echo '+memory +pids' > "$SDUOJ_CGROUP_MOUNT/cgroup.subtree_control"
mkdir "$SDUOJ_CGROUP_ROOT"
echo '+memory +pids' > "$SDUOJ_CGROUP_ROOT/cgroup.subtree_control"
exec "$@"
