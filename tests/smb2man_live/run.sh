#!/bin/sh
# Live check of the smb: driver (iop/smb2man/smb2man.c) on the host, against a real Samba server.
#
# drive.c compiles the driver into itself and drives its iomanX ops and devctls the way the EE does
# (src/smb_shares.c, the file browser): two shares and a guest share connected at once by name, the
# device root listing them, -EEXIST for a name already connected, mkdir/write/read/seek/rename/
# remove/rmdir, rename across shares refused, names past 127 characters, a session the server
# closed (connected again on the next use, its handles -EBADF), a READ on a dead socket, the 30 s
# reconnect backoff (clock driven by the harness), each CONNECT refusal (password, share, a host
# name, bad URL, nothing listening, an address that never answers within the 5 s bound, a ninth
# share), and DISCONNECT_ALL. Then a user name with a Latin-1 letter, the dialect and signing cases: signing mandatory on SMB 2.x
# (every reply verified), signing off, the server capped at 2.0.2, SMB 3.0-and-up (3.0.2 expected),
# SMB 3.x with signing mandatory (refused: SIGNING) and 3.1.1 only (refused: PROT).
#
# Built with AddressSanitizer and UBSan; a sanitizer report, a leak included, fails the case.
# Needs a C compiler and Samba (smbd, smbclient). Run as root (smbd wants to own its state):
#   sh tests/smb2man_live/run.sh
# This is a host test, not a console test: it does not exercise the IOP, its ps2ip stack or
# real hardware.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
port=${SMB2_LIVE_PORT:-4451}
command -v smbd >/dev/null || { echo "smbd not installed (apt-get install samba smbclient)"; exit 2; }

work=$(mktemp -d)
cleanup() {
    [ -f "$work/run/smbd.pid" ] && kill "$(cat "$work/run/smbd.pid")" 2>/dev/null || true
    pkill -f -- "--configfile=$work/smb.conf" 2>/dev/null || true
    [ -f "$work/made-jose" ] && userdel 'josé' 2>/dev/null || true
    [ -f "$work/made-ps2" ] && userdel ps2 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

mkdir -p "$work/share/DVD" "$work/other" "$work/run/ncalrpc"
head -c 1048576 /dev/urandom >"$work/share/DVD/TEST.iso"
printf 'other\n' >"$work/other/only-in-other.txt"

conf="$work/smb.conf"
write_conf() { # $1 signing, $2 extra global lines
    cat >"$conf" <<EOF
[global]
   server role = standalone server
   map to guest = bad user
   smb ports = $port
   interfaces = 127.0.0.1
   bind interfaces only = yes
   server min protocol = SMB2_02
   server signing = $1
   ntlm auth = ntlmv2-only
   $2
   ncalrpc dir = $work/run/ncalrpc
   pid directory = $work/run
   lock directory = $work/run
   state directory = $work/run
   cache directory = $work/run
   private dir = $work/run
   log file = $work/run/log.%m
   disable netbios = yes
   load printers = no
[PS2SMB]
   path = $work/share
   read only = no
   force user = root
   guest ok = yes
[OTHER]
   path = $work/other
   read only = no
   force user = root
EOF
}

start_smbd() {
    if [ -f "$work/run/smbd.pid" ]; then
        oldpid=$(cat "$work/run/smbd.pid")
        kill "$oldpid" 2>/dev/null || true
        i=0
        while kill -0 "$oldpid" 2>/dev/null; do
            i=$((i + 1))
            [ $i -lt 50 ] || { echo "previous smbd ($oldpid) did not exit"; exit 1; }
            sleep 0.2
        done
    fi
    smbd -s "$conf" -D || { echo "smbd did not start"; exit 1; }
    i=0
    until smbclient -s "$conf" -p "$port" //127.0.0.1/PS2SMB -U ps2%secret -m SMB3 -c ls >/dev/null 2>&1; do
        i=$((i + 1))
        [ $i -lt 50 ] || { echo "smbd did not come up"; tail -20 "$work/run/log.smbd" 2>/dev/null; exit 1; }
        sleep 0.2
    done
}

# ---- build: as ps2.yaml builds the IRX, plus the sanitizers ---------------------------------------
export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}exitcode=86"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}exitcode=86"
# usmb2 stores 64-bit header fields at 4-byte-aligned offsets of its buffer, which the IOP (32-bit
# MIPS, two word stores) takes and UBSan's alignment check does not: that one check is off.
san="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-sanitize=alignment -fno-omit-frame-pointer -g -O1"
feat="-DUSMB2_FEATURE_WRITE -DUSMB2_FEATURE_NTLM -DUSMB2_FEATURE_UNICODE -DUSMB2_FEATURE_STAT -DUSMB2_FEATURE_MKDIR \
 -DUSMB2_FEATURE_UNLINK -DUSMB2_FEATURE_RENAME"
alloc="-Dmalloc=iop_malloc -Dfree=iop_free -Dcalloc=iop_calloc"
u2="$root/iop/smb2man/usmb2"
mkdir -p "$work/lib"
for f in ntlm md4c hmac-md5 md5 unicode; do
    # shellcheck disable=SC2086
    cc $san -w $feat $alloc -c "$u2/$f.c" -o "$work/lib/$f.o"
done
# shellcheck disable=SC2086
cc $san -Wall -I"$here/inc" -c "$root/iop/smb2man/smb2auth.c" -o "$work/lib/smb2auth.o"
# shellcheck disable=SC2086
cc $san -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Werror -I"$here/inc" -I"$root/iop/smb2man" $feat $alloc \
    -D_start=smb2man_start "$here/drive.c" "$work"/lib/*.o -o "$work/drive"

if ! id ps2 >/dev/null 2>&1; then
    useradd -M ps2
    : >"$work/made-ps2" # removed again on exit
fi
printf 'secret\nsecret\n' | smbpasswd -c "$conf" -s -a ps2 >/dev/null 2>&1 || true

fails=0
run() { # $1 label, then the command
    label=$1
    shift
    if (cd "$work" && "$@") >"$work/out.txt" 2>&1; then
        echo "ok   $label"
    else
        echo "FAIL $label"
        grep -E '^FAIL|ERROR: [A-Za-z]+Sanitizer|runtime error|^SUMMARY: ' "$work/out.txt" || true
        tail -15 "$work/out.txt"
        fails=$((fails + 1))
    fi
    [ -n "${SMB2_LIVE_VERBOSE:-}" ] && cat "$work/out.txt" || true
}
drive() { # $1 mode
    "$work/drive" "$1" "$port" ps2 secret "$work/share" "$work/other"
}

# smbpasswd needs the configuration in place first
write_conf auto ""
printf 'secret\nsecret\n' | smbpasswd -c "$conf" -s -a ps2 >/dev/null
start_smbd
run "every case, signing negotiable (2.1)" drive full
run "dialect 2.1, signing not required (unsigned)" drive dialect=0210,0
# NTOWFv2 hashes the user name in upper case: a Latin-1 letter must be upper-cased as the server does it
if ! id 'josé' >/dev/null 2>&1; then
    useradd -M 'josé' 2>/dev/null || useradd --badname -M 'josé'
    : >"$work/made-jose" # removed again on exit
fi
printf 'secret\nsecret\n' | smbpasswd -c "$conf" -s -a 'josé' >/dev/null
run "user name with a Latin-1 letter (josé)" "$work/drive" dialect=0210,0 "$port" 'josé' secret "$work/share" "$work/other"

write_conf mandatory ""
start_smbd
run "signing mandatory on 2.1: requests signed, every reply verified" drive dialect=0210,1

write_conf auto "server max protocol = SMB2_02"
start_smbd
run "server capped at 2.0.2" drive dialect=0202,0

write_conf auto "server min protocol = SMB3_00
   server max protocol = SMB3_02"
start_smbd
run "SMB 3.0 and up: 3.0.2" drive dialect=0302,0

write_conf mandatory "server min protocol = SMB3_00
   server max protocol = SMB3_02"
start_smbd
run "SMB 3.x with signing mandatory: refused (SIGNING)" drive refuse=1004

write_conf auto "server min protocol = SMB3_11"
start_smbd
run "SMB 3.1.1 only: refused (PROT)" drive refuse=1003

echo "smb2man live: $fails failure(s)"
[ "$fails" -eq 0 ]
