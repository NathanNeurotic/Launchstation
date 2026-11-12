#!/bin/sh
# Live check of the nfs: driver (iop/nfsman/nfsman.c) on the host, against a real NFS server.
#
# drive.c compiles the driver into itself and drives its iomanX ops and devctls the way the EE does:
# two exports connected at once by name, the device root listing them, -EEXIST for a name already
# connected, each CONNECT refusal (not nfs://, a host name, an unknown option, nothing listening,
# no such export), mkdir/write/read/seek, O_TRUNC, O_APPEND and O_EXCL, rename (and across exports,
# refused), the errors of remove/rmdir/open, a 255 byte name and a path that does not fit, a
# connection the server closed (connected again on the next use, open files still good), the 30 s
# reconnect backoff (clock driven by the harness), and DISCONNECT_ALL.
#
# Built with AddressSanitizer and UBSan; a sanitizer report, a leak included, fails the case.
#
# By default it exports a scratch directory from the kernel NFS server (nfs-kernel-server, NFSv4.0
# enabled) and needs root:
#   sh tests/nfsman_live/run.sh
# or it uses an export that is already there, with an empty directory in it the test may use:
#   sh tests/nfsman_live/run.sh <server-ip> <export> <dir-in-export>
# This is a host test, not a console test: it does not exercise the IOP, its ps2ip stack or
# real hardware.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
work=$(mktemp -d)
exported=""
cleanup() {
    [ -n "$exported" ] && exportfs -u "127.0.0.1:$exported" 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [ $# -eq 3 ]; then
    server=$1 export=$2 dir=$3
else
    command -v exportfs >/dev/null || { echo "exportfs not installed (apt-get install nfs-kernel-server)"; exit 2; }
    grep -q '+4.0\|+4 ' /proc/fs/nfsd/versions 2>/dev/null || { echo "nfsd is not running with NFSv4.0"; exit 2; }
    mkdir -p "$work/export/test"
    chmod 777 "$work/export/test"
    exportfs -o rw,insecure,no_root_squash,no_subtree_check,fsid=4711 "127.0.0.1:$work/export"
    exported="$work/export"
    server=127.0.0.1 export="$work/export" dir=test
fi

${CC:-cc} -std=gnu99 -g -O1 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -D_start=nfsman_start \
    -I "$root/tests/smb2man_live/inc" -I "$root/iop/nfsman" \
    "$here/drive.c" -o "$work/drive"
"$work/drive" "$server" "$export" "$dir"
