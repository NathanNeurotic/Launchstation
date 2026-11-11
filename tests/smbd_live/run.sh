#!/bin/sh
# Live check of the SMB file server (iop/smbd/smbd_fs.c) on the host, driven by Samba's smbclient.
#
# host.c compiles the server's request handlers with a POSIX stand-in for the memory cards
# (ROOT/mc0, ROOT/mc1) and libsmb2 (fetched by scripts/fetch_libsmb2.sh) built for the host.
# Checked: every dialect from 2.0.2 to 3.1.1 with signing required, the wrong password, the
# wrong user and guest refused, unknown shares and IPC$ refused, both shares, a 1 MiB file both
# ways byte for byte, a 200-file folder listed whole, mkdir/rename/delete/rmdir, rename onto an
# existing name refused, a non-empty folder kept, a replacing move to another folder refused, several files
# open at once with the card's descriptor limit modelled, ".." kept inside the share, a full card (the
# write fails and the client is told), a third client refused, and Stop (SIGTERM ->
# smbd_stop()) returning within a second with nothing leaked.
#
# Built with AddressSanitizer and UBSan; a sanitizer report, a leak included, fails the run.
# Needs a C compiler, cmake, git and smbclient:  sh tests/smbd_live/run.sh
# This is a host test: it does not exercise the IOP, its ps2ip stack, MCMAN or real hardware.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
port=${SMBD_LIVE_PORT:-4455}
command -v smbclient >/dev/null || { echo "smbclient not installed (apt-get install smbclient)"; exit 2; }

sh "$root/scripts/fetch_libsmb2.sh"
lib="$root/.smbd-deps/libsmb2"
work=$(mktemp -d)
pid=
cleanup() {
    [ -n "$pid" ] && kill "$pid" 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT

san="-fsanitize=address,undefined -fno-sanitize-recover=undefined -g -O1"
cmake -S "$lib" -B "$work/lib" -DBUILD_SHARED_LIBS=OFF -DENABLE_EXAMPLES=OFF -DENABLE_GSSAPI=OFF \
    -DENABLE_LIBKRB5=OFF -DENABLE_LIBQUIC=OFF -DENABLE_RDMA=OFF -DCMAKE_C_FLAGS="$san" >"$work/cmake.log" 2>&1 ||
    { cat "$work/cmake.log"; exit 1; }
make -C "$work/lib" -j"$(nproc)" smb2 >"$work/make.log" 2>&1 || { tail -40 "$work/make.log"; exit 1; }
cc $san -Wall -Wextra -Werror -DHAVE_CONFIG_H -I"$work/lib" -I"$root/iop/smbd" -I"$lib/include" \
    -I"$lib/include/smb2" -I"$lib/lib" "$here/host.c" "$root/iop/smbd/smbd_fs.c" "$work/lib/lib/libsmb2.a" \
    -o "$work/host"
cc $san -Wall -Wextra -Werror -I"$lib/include" "$here/client.c" "$work/lib/lib/libsmb2.a" -o "$work/client"

fail=0
pass() { echo "PASS: $1"; }
bad() { echo "FAIL: $1"; fail=1; }

start() { # $1 free KB (optional)
    rm -rf "$work/root" && mkdir -p "$work/root/mc0/BESLES-12345" "$work/root/mc1"
    printf 'icon\n' >"$work/root/mc0/BESLES-12345/icon.sys"
    ASAN_OPTIONS=detect_leaks=1 "$work/host" "$port" "$work/root" ps2 secret ${1:-} >"$work/host.log" 2>&1 &
    pid=$!
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        smbclient -N -L //127.0.0.1 -p "$port" >/dev/null 2>&1 && break
        sleep 0.2
    done
}

stop() { # SIGTERM is smbd_stop(); the server has to return within a second, cleanly
    kill -TERM "$pid"
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.1
    done
    if kill -0 "$pid" 2>/dev/null; then
        bad "stop: still running 1 s after Stop"
        kill -9 "$pid"
    fi
    wait "$pid" && rc=0 || rc=$?
    pid=
    if grep -q "ERROR: AddressSanitizer\|runtime error:\|LeakSanitizer" "$work/host.log"; then
        bad "sanitizer report"
        cat "$work/host.log"
    elif [ "$rc" -ne 0 ]; then
        bad "stop: exit $rc"
        cat "$work/host.log"
    else
        pass "stop returned cleanly ($1)"
    fi
}

smb() { # $1 share, $2 commands, then extra smbclient options
    share=$1 cmds=$2
    shift 2
    smbclient "//127.0.0.1/$share" -p "$port" -U ps2%secret --option='client signing=required' "$@" -c "$cmds" 2>&1
}

start
for proto in SMB2_02 SMB2_10 SMB3_00 SMB3_02 SMB3_11; do
    if out=$(smb mc0 'ls BESLES-12345\icon.sys' -m "$proto" --option="client min protocol=$proto") &&
        echo "$out" | grep -q 'icon.sys'; then
        pass "dialect $proto, signed"
    else
        bad "dialect $proto: $out"
    fi
done

out=$(smbclient //127.0.0.1/mc0 -p "$port" -U ps2%wrong -c ls 2>&1) && bad "wrong password accepted" ||
    { echo "$out" | grep -q LOGON_FAILURE && pass "wrong password refused" || bad "wrong password: $out"; }
out=$(smbclient //127.0.0.1/mc0 -p "$port" -U other%secret -c ls 2>&1) && bad "wrong user accepted" ||
    pass "wrong user refused"
out=$(smbclient //127.0.0.1/mc0 -p "$port" -N -c ls 2>&1) && bad "guest accepted" || pass "guest refused"
out=$(smb hdd0 ls) && bad "unknown share accepted" ||
    { echo "$out" | grep -q BAD_NETWORK_NAME && pass "unknown share refused" || bad "unknown share: $out"; }
out=$(smb 'IPC$' ls) && bad "IPC\$ accepted" || pass "IPC\$ refused"
smb MC1 ls | grep -q 'blocks available' && pass "mc1 share (name without case)" || bad "mc1 share"

head -c 1048576 /dev/urandom >"$work/big.bin"
smb mc0 "put $work/big.bin BIG.BIN; get BIG.BIN $work/big.back" >/dev/null
cmp -s "$work/big.bin" "$work/big.back" && cmp -s "$work/big.bin" "$work/root/mc0/BIG.BIN" &&
    pass "1 MiB file both ways, byte for byte" || bad "1 MiB round trip differs"

mkdir "$work/root/mc0/MANY"
i=0
while [ $i -lt 200 ]; do : >"$work/root/mc0/MANY/file-with-a-longish-name-$i.dat"; i=$((i + 1)); done
n=$(smb mc0 'ls MANY\*' | grep -c 'file-with-a-longish-name-') || true
[ "$n" -eq 200 ] && pass "200-file folder listed whole" || bad "200-file folder: $n entries"

out=$(smb mc0 "mkdir NEWDIR; put $work/big.bin NEWDIR\\A.BIN; rename NEWDIR\\A.BIN NEWDIR\\B.BIN; ls NEWDIR\\*")  || true
[ -f "$work/root/mc0/NEWDIR/B.BIN" ] && [ ! -f "$work/root/mc0/NEWDIR/A.BIN" ] && pass "mkdir, put, rename" ||
    bad "mkdir/put/rename: $out"
out=$(smb mc0 "put $work/big.bin NEWDIR\\C.BIN; rename NEWDIR\\C.BIN NEWDIR\\B.BIN") || true
echo "$out" | grep -q OBJECT_NAME_COLLISION && [ -f "$work/root/mc0/NEWDIR/C.BIN" ] &&
    pass "rename onto an existing name refused" || bad "rename collision: $out"
out=$(smb mc0 "rmdir NEWDIR") || true
[ -d "$work/root/mc0/NEWDIR" ] && echo "$out" | grep -q DIRECTORY_NOT_EMPTY &&
    pass "non-empty folder kept, and the client told" || bad "non-empty rmdir: $out"
mkdir "$work/root/mc0/OTHER" && printf 'keep\n' >"$work/root/mc0/OTHER/B.BIN"
out=$(smb mc0 "rename NEWDIR\\C.BIN OTHER\\B.BIN -f") || true
echo "$out" | grep -q NOT_SAME_DEVICE && [ "$(cat "$work/root/mc0/OTHER/B.BIN")" = keep ] &&
    cmp -s "$work/big.bin" "$work/root/mc0/NEWDIR/C.BIN" &&
    pass "replacing move to another folder refused, target kept" || bad "cross-folder replace: $out"
rm -r "$work/root/mc0/OTHER"
printf 'keep\n' >"$work/root/mc0/NEWDIR/FAILONCE.BIN"
out=$(smb mc0 "rename NEWDIR\\C.BIN NEWDIR\\FAILONCE.BIN -f") || true
echo "$out" | grep -q NT_STATUS_ && [ "$(cat "$work/root/mc0/NEWDIR/FAILONCE.BIN")" = keep ] &&
    cmp -s "$work/big.bin" "$work/root/mc0/NEWDIR/C.BIN" &&
    [ ! -e "$work/root/mc0/NEWDIR/SMBD-OLD.TMP" ] &&
    pass "failed replacing rename puts the target back" || bad "failed replace: $out"
cp "$work/root/mc0/NEWDIR/C.BIN" "$work/c.expect"
out=$(smb mc0 "rename NEWDIR\\C.BIN NEWDIR\\FAILONCE.BIN -f") || true
cmp -s "$work/root/mc0/NEWDIR/FAILONCE.BIN" "$work/c.expect" && [ ! -e "$work/root/mc0/NEWDIR/C.BIN" ] &&
    [ ! -e "$work/root/mc0/NEWDIR/SMBD-OLD.TMP" ] &&
    pass "replacing rename" || bad "replace: $out"
rm "$work/root/mc0/NEWDIR/FAILONCE.BIN"
smb mc0 "del NEWDIR\\B.BIN; del NEWDIR\\C.BIN; rmdir NEWDIR" >/dev/null || true
[ ! -e "$work/root/mc0/NEWDIR" ] && pass "delete and rmdir" || bad "delete/rmdir"

printf aaaa >"$work/root/mc0/A.TXT" && printf bbbb >"$work/root/mc0/B.TXT"
if out=$(ASAN_OPTIONS=detect_leaks=0 "$work/client" "127.0.0.1:$port" ps2 secret 2>&1); then
    pass "four files open at once, a listing while they are open, within the card's descriptors"
else
    bad "files open at once: $out"
fi
rm -f "$work/root/mc0/A.TXT" "$work/root/mc0/B.TXT" "$work/root/mc0/C.TXT"

out=$(smb mc0 'get ..\..\host.log '"$work/escaped" 2>&1) || true
[ ! -f "$work/escaped" ] && pass "\"..\" stays inside the share" || bad "path escaped the share"
stop "after the protocol checks"

start 1200 # about 1 MiB left on the "card"
head -c 2097152 /dev/urandom >"$work/huge.bin"
out=$(smb mc0 "put $work/huge.bin HUGE.BIN") && bad "write past a full card reported as done" ||
    { echo "$out" | grep -q 'DISK_FULL' && pass "full card: the client is told" || bad "full card: $out"; }
stop "after a full card"

start
hold() { # $1 share, $2 seconds: a client that stays connected
    (echo ls; sleep "$2"; echo exit) | smbclient "//127.0.0.1/$1" -p "$port" -U ps2%secret >/dev/null 2>&1
}
hold mc0 3 &
c1=$!
hold mc1 3 &
c2=$!
sleep 1
out=$(smb mc0 ls) && bad "third client accepted" || pass "third client refused"
wait $c1 $c2 || true
stop "with clients come and gone"

start
(echo "put $work/big.bin OPEN.BIN"; sleep 2; echo exit) |
    smbclient //127.0.0.1/mc0 -p "$port" -U ps2%secret >/dev/null 2>&1 &
c1=$!
sleep 1
stop "with a client still connected"
wait $c1 || true

[ $fail -eq 0 ] && echo "smbd live: all passed" || echo "smbd live: FAILED"
exit $fail
