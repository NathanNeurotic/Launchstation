"""Exercise PSBBN system, language and optional Linux preflight with real gzip/TAR inputs."""
from pathlib import Path
import ctypes as c
import gzip
import io
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = [
    '__system/p2lboot/osdboot.elf', '__system/p2lboot/vmlinux',
    '__linux.1/sbin/init', '__linux.1/bin/sh', '__linux.1/etc/fstab',
    '__linux.4/bn/script/utility/sysconf.xml',
]
OSD = ['OSDSYS_A.XLF', 'FNTOSD', 'ICOIMAGE', 'JISUCS', 'SKBIMAGE', 'SNDIMAGE', 'TEXIMAGE']

HOST_HEADER = r'''
#ifndef LAUNCHELF_H
#define LAUNCHELF_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
typedef uint64_t u64;
typedef int64_t s64;
typedef struct { unsigned mode, attr, size, private_0, private_5; } iox_stat_t;
#define MAX_PATH 1025
#define FIO_O_RDONLY O_RDONLY
#define FIO_O_WRONLY O_WRONLY
#define FIO_O_RDWR O_RDWR
#define FIO_O_CREAT O_CREAT
#define FIO_O_EXCL O_EXCL
#define FIO_O_TRUNC O_TRUNC
#define FIO_MT_RDWR 1
#define FIO_S_ISDIR(m) (((m) & 0xf000) == 0x4000)
#define FS_GROUP_APPLICATION 1
#define HDIOC_FLUSH 1
void dashWorkerStatus(const char *, int, int);
int genOpen(const char *, int);
int genRead(int, void *, unsigned);
int genWrite(int, const void *, unsigned);
int genClose(int);
s64 genLseek(int, s64, int);
int genMkdir(const char *, int);
int fileXioOpen(const char *, int, int);
int fileXioClose(int);
int fileXioGetStat(const char *, iox_stat_t *);
int fileXioMount(const char *, const char *, int);
int fileXioUmount(const char *);
int fileXioDevctl(const char *, int, const void *, unsigned, void *, unsigned);
int hddMakeFilesystem(int, char *, int);
#endif
'''

STUBS = r'''
#include "launchelf.h"
#include "hdd_install.h"
#include "install_ext2.h"
void hddInstallLog(const char *text, ...) { (void)text; }
void hddInstallLogFlush(HddInstall *in) { (void)in; }
void hddInstallStatus(const char *text, int done, int total) { (void)text; (void)done; (void)total; }
void hddInstallProgress(const char *text, int done, int total) { (void)text; (void)done; (void)total; }
void hddInstallOverall(uint64_t done, uint64_t total) { (void)done; (void)total; }
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
void dashWorkerStatus(const char *s,int d,int t) {(void)s;(void)d;(void)t;}
int genOpen(const char *p,int mode) {return open(p,mode,0600);}
int fileXioOpen(const char *p,int mode,int permissions) {(void)permissions;return open(p,mode,0600);}
int fileXioClose(int fd) {return close(fd);}
int genRead(int fd,void *p,unsigned n) {return read(fd,p,n);}
int genWrite(int fd,const void*p,unsigned n) {return write(fd,p,n);}
int genClose(int fd) {return close(fd);}
s64 genLseek(int fd,s64 p,int w) {return lseek(fd,p,w);}
int genMkdir(const char*p,int m) {return mkdir(p,m);}
int genRemove(char*p) {return unlink(p);}
int genRmdir(char*p) {return rmdir(p);}
int fileXioGetStat(const char*p,iox_stat_t*s) {(void)p;(void)s;return -ENOENT;}
int fileXioMount(const char*m,const char*p,int f) {(void)m;(void)p;(void)f;return 0;}
int fileXioUmount(const char*m) {(void)m;return 0;}
int fileXioDevctl(const char*p,int c,const void*d,unsigned ds,void*o,unsigned os) {(void)p;(void)c;(void)d;(void)ds;(void)o;(void)os;return 0;}
int hddCheckAllocation(uint32_t s) {(void)s;return 0;}
int hddMakeFilesystem(int mb,char*n,int g) {(void)n;(void)g;return mb;}
int installExt2Begin(InstallExt2*f,const InstallTarEntry*e) {(void)f;(void)e;return 0;}
int installExt2Data(InstallExt2*f,const void*d,unsigned n) {(void)f;(void)d;(void)n;return 0;}
int installExt2End(InstallExt2*f) {(void)f;return 0;}
int installExt2Create(InstallExt2**f,const HddFormatIo*i,uint32_t s,uint32_t n,uint32_t t) {(void)f;(void)i;(void)s;(void)n;(void)t;return 0;}
int installExt2Finish(InstallExt2*f) {(void)f;return 0;}
void installExt2Free(InstallExt2*f) {(void)f;}
int installPsbbnSwap(const HddFormatIo*i,uint32_t s,uint32_t n) {(void)i;(void)s;(void)n;return 0;}
int installPsbbnMusic(const HddFormatIo*i,uint32_t s,uint32_t n,uint32_t t) {(void)i;(void)s;(void)n;(void)t;return 0;}
int hddInstallWriteConfigs(HddInstall*i) {(void)i;return 0;}
int hddInstallCheckAsset(HddInstall*i,unsigned a) {(void)i;(void)a;return 0;}
size_t hddInstallFixtureSize(void) { return sizeof(HddInstall); }
size_t hddInstallFixtureLanguageOffset(void) { return offsetof(HddInstall, language); }
size_t hddInstallFixtureCleanupOffset(void) { return offsetof(HddInstall, cleanup_pending); }
'''


def archive(entries):
    out = io.BytesIO()
    with tarfile.open(fileobj=out, mode='w', format=tarfile.GNU_FORMAT) as tar:
        for name, kind, data in entries:
            entry = tarfile.TarInfo(name)
            entry.type = kind.encode()
            entry.mode = 0o755
            if kind == '0':
                entry.size = len(data)
                tar.addfile(entry, io.BytesIO(data))
            else:
                if kind in ('1', '2'):
                    entry.linkname = data
                tar.addfile(entry)
    return gzip.compress(out.getvalue(), mtime=0)


def base_entries():
    entries = []
    parents = set()
    for path in REQUIRED:
        parts = path.split('/')
        for i in range(1, len(parts)):
            parents.add('/'.join(parts[:i]))
    entries.extend((path, '5', '') for path in sorted(parents))
    entries.extend((path, '0', b'payload') for path in REQUIRED)
    entries.extend(('__system/osd100/' + name, '0', b'payload') for name in OSD)
    return entries


with tempfile.TemporaryDirectory(prefix='hdd-preflight-') as directory:
    work = Path(directory)
    (work / 'stubs.c').write_text(STUBS)
    (work / 'launchelf.h').write_text(HOST_HEADER)
    (work / 'dash_internal.h').write_text('#ifndef DASH_INTERNAL_H\n#define DASH_INTERNAL_H\n#endif\n')
    library = work / 'preflight.so'
    subprocess.run(['gcc', '-std=c99', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', '-I', str(work), '-I', str(ROOT / 'include'), str(work / 'stubs.c'),
                    str(ROOT / 'src/hdd_install_psbbn.c'), str(ROOT / 'src/hdd_install_plan.c'),
                    str(ROOT / 'src/install_tar.c'), '-lz', '-o', str(library)], check=True)
    lib = c.CDLL(str(library))
    lib.hddInstallPsbbnPreflight.argtypes = [c.c_void_p]
    lib.hddInstallPsbbnPreflight.restype = c.c_int
    class Language(c.Structure):
        _fields_ = [('code', c.c_char * 4), ('name', c.c_char * 48)]

    class Install(c.Structure):
        _fields_ = [('flags', c.c_uint), ('jail', c.c_int), ('commit', c.c_char * 41),
                    ('stage', c.c_char * 1024), ('ca_path', c.c_char * 1024), ('error', c.c_char * 256),
                    ('sizes', c.c_uint32 * 15), ('hashes', (c.c_char * 41) * 15),
                    ('archives', (c.c_char * 128) * 2), ('linux', c.c_uint32 * 20),
                    ('owner', c.c_ubyte * 20), ('owned', c.c_uint), ('ready', c.c_uint),
                    ('ca_data', c.c_void_p), ('ca_size', c.c_uint),
                    ('music_mb', c.c_uint), ('contents_mb', c.c_uint), ('linux_home_mb', c.c_uint),
                    ('reserved_mb', c.c_uint), ('game_launcher', c.c_uint),
                    ('launcher_files', c.c_void_p), ('launcher_count', c.c_uint), ('language', Language),
                    ('archive_item', c.c_char * 96), ('channels', c.c_char * 128),
                    ('download_bytes', c.c_uint64), ('cleanup_pending', c.c_uint)]

    for name in ('Size', 'LanguageOffset', 'CleanupOffset'):
        getattr(lib, 'hddInstallFixture' + name).restype = c.c_size_t
    assert c.sizeof(Install) == lib.hddInstallFixtureSize()
    assert Install.language.offset == lib.hddInstallFixtureLanguageOffset()
    assert Install.cleanup_pending.offset == lib.hddInstallFixtureCleanupOffset()

    def run(system, language, linux=None):
        (work / 'system.tar.gz').write_bytes(archive(system))
        (work / 'language.tar.gz').write_bytes(archive(language))
        install = Install(flags=15 if linux is not None else 7, jail=1)
        install.stage = str(work).encode()
        install.sizes[6] = (work / 'system.tar.gz').stat().st_size
        install.sizes[7] = (work / 'language.tar.gz').stat().st_size
        if linux is not None:
            (work / 'PS2Linux.tar.gz').write_bytes(archive(linux))
            install.sizes[10] = (work / 'PS2Linux.tar.gz').stat().st_size
        return lib.hddInstallPsbbnPreflight(c.byref(install))

    valid = base_entries()
    result = run(valid, [])
    assert result == 0
    assert run(valid, [('__system/p2lboot/vmlinux', '0', b'replacement')]) == 0
    assert run(valid, [('__system/p2lboot/vmlinux', '0', b'')]) < 0

    linux = [('sbin', '5', ''), ('bin', '5', ''), ('sbin/init', '0', b'init'), ('bin/sh', '2', 'bash')]
    assert run(valid, [], linux) == 0
    assert run(valid, [], linux + [('sbin/init', '0', b'')]) < 0
    assert run(valid, [], linux + [('bin/sh', '0', b'')]) < 0
    assert run(valid, [], linux + [('bin/sh', '5', '')]) < 0
    recovered_linux = linux[:2] + [('sbin/init', '0', b''), ('bin/sh', '5', '')] + linux[2:]
    assert run(valid, [], recovered_linux) == 0

    missing = [entry for entry in valid if entry[0] != '__linux.1/bin']
    assert run(missing, []) == 0
    assert run(missing, [('__linux.1/bin', '5', '')]) == 0
    assert run(missing, [('__linux.1/bin', '0', b'not a directory')]) < 0

    nested = valid + [('__linux.1/opt/tools/file', '0', b'x'),
                      ('__linux.1/opt/tools', '5', ''), ('__linux.1/opt', '5', '')]
    assert run(nested, []) == 0

    conflict = [entry for entry in valid if not entry[0].startswith('__linux.1/opt')]
    conflict.append(('__linux.1/opt', '2', '/tmp'))
    assert run(conflict, [('__linux.1/opt/extra', '0', b'x')]) < 0

    linked = valid + [('__linux.1/bin/linked', '1', '__linux.1/bin/sh')]
    assert run(linked, []) == 0
    assert run(valid + [('__linux.1/bin/linked', '1', '__linux.1/bin/missing')], []) == -2
    assert run(valid + [('__linux.1/bin/self', '1', '__linux.1/bin/self')], []) == -2
    assert run(valid + [('__linux.1/bin/linked', '1', '__linux.1/bin')], []) == -22

print('PASS: PSBBN preflight accepts valid overlays and implicit parents, tracks Linux requirements, and rejects path conflicts and invalid hard links')
