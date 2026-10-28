#include <errno.h>
#include <iomanX.h>
#include <string.h>
#include <tamtypes.h>

#include "mmce_sio2.h"
#include "mmce_cmds.h"
#include "mmce_restore_guard.h"

#include "module_debug.h"

#define MMCE_FS_MAX_FD 16
#define MMCE_FS_WAIT_TIMEOUT 128000

/* Synthetic read-only view of the exact backing image currently selected by
 * the card firmware. It is intentionally not listed by dread; callers must
 * opt in by opening this exact name. */
#define MMCE_FS_ACTIVE_HANDLE 250

#define NOT_SUPPORTED_OP (void*)&not_supported_operation
static int not_supported_operation() {
    return -ENOTSUP;
}

static int mmce_fs_fds[MMCE_FS_MAX_FD];
static int mmce_fs_active_fd[MMCE_FS_MAX_FD];
static s64 mmce_fs_active_pos[MMCE_FS_MAX_FD];
static s64 mmce_fs_active_size[MMCE_FS_MAX_FD];
static int last_unit = -1;

/* Forward declarations used by the synthetic active-card handle. */
int mmce_fs_read(iomanX_iop_file_t *file, void *ptr, int size);
s64 mmce_fs_lseek64(iomanX_iop_file_t *file, s64 offset, int whence);

static int mmce_fs_slot_index(iomanX_iop_file_t *file)
{
    int *slot = (int *)file->privdata;

    if (slot == NULL || slot < &mmce_fs_fds[0] || slot >= &mmce_fs_fds[MMCE_FS_MAX_FD])
        return -1;
    return slot - &mmce_fs_fds[0];
}

static int mmce_fs_is_active_name(const char *name)
{
    if (name == NULL)
        return 0;
    while (*name == '/')
        name++;
    return strcmp(name, MMCE_FS_ACTIVE_NAME) == 0;
}

static int mmce_fs_has_open_handles(void)
{
    int i;

    for (i = 0; i < MMCE_FS_MAX_FD; i++) {
        if (mmce_fs_fds[i] >= 0)
            return 1;
    }
    return 0;
}

static s64 mmce_fs_active_seek(iomanX_iop_file_t *file, s64 offset, int whence)
{
    int slot = mmce_fs_slot_index(file);
    s64 base, next;

    if (slot < 0 || mmce_fs_fds[slot] != MMCE_FS_ACTIVE_HANDLE)
        return -EBADF;

    switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = mmce_fs_active_pos[slot];
            break;
        case SEEK_END:
            base = mmce_fs_active_size[slot];
            break;
        default:
            return -EINVAL;
    }

    next = base + offset;
    if (next < 0)
        return -EINVAL;

    mmce_fs_active_pos[slot] = next;
    return next;
}

static int mmce_fs_discover_active_fd(iomanX_iop_file_t *file, int *active_fd, s64 *active_size)
{
    iomanX_iop_file_t probe_file;
    int probe_fd, found_fd = -1;
    s64 found_pos = -1, pos, end;

    /* MMCE RESET deliberately closes every firmware filesystem descriptor
     * except cardman_fd. Never do that underneath another local MMCE handle,
     * or while firmware reports a card switch/create operation in progress. */
    if (mmce_fs_has_open_handles())
        return -EBUSY;
    {
        int status = mmce_cmd_get_status();
        if (status < 0)
            return -EIO;
        if (status & 1)
            return -EBUSY;
    }
    if (mmce_cmd_reset() != 0)
        return -EIO;

    memset(&probe_file, 0, sizeof(probe_file));
    probe_file.unit = file->unit;

    for (probe_fd = 0; probe_fd < MMCE_FS_MAX_FD; probe_fd++) {
        int raw_fd = probe_fd;
        probe_file.privdata = &raw_fd;
        pos = mmce_fs_lseek64(&probe_file, 0, SEEK_CUR);
        if (pos < 0)
            continue;
        if (found_fd >= 0) {
            DPRINTF("%s ERROR: More than one descriptor survived MMCE reset (%i, %i)\n",
                    __func__, found_fd, probe_fd);
            return -EIO;
        }
        found_fd = probe_fd;
        found_pos = pos;
    }

    if (found_fd < 0)
        return -ENOENT;

    probe_file.privdata = &found_fd;
    end = mmce_fs_lseek64(&probe_file, 0, SEEK_END);
    if (end <= 0 || mmce_fs_lseek64(&probe_file, found_pos, SEEK_SET) != found_pos)
        return -EIO;

    *active_fd = found_fd;
    *active_size = end;
    DPRINTF("%s: active card backing fd %i, size %lld\n",
            __func__, found_fd, (long long)end);
    return 0;
}

static int mmce_fs_active_read(iomanX_iop_file_t *file, void *ptr, int size)
{
    iomanX_iop_file_t backing;
    int slot = mmce_fs_slot_index(file);
    int raw_fd, result;
    s64 saved_pos, want, restore;

    if (slot < 0 || mmce_fs_fds[slot] != MMCE_FS_ACTIVE_HANDLE || size < 0)
        return -EBADF;
    if (size == 0 || mmce_fs_active_pos[slot] >= mmce_fs_active_size[slot])
        return 0;

    if ((s64)size > mmce_fs_active_size[slot] - mmce_fs_active_pos[slot])
        size = (int)(mmce_fs_active_size[slot] - mmce_fs_active_pos[slot]);

    raw_fd = mmce_fs_active_fd[slot];
    memset(&backing, 0, sizeof(backing));
    backing.unit = file->unit;
    backing.privdata = &raw_fd;

    /* cardman uses the same firmware descriptor. Preserve its live position
     * around our virtual read so emulation state is not left displaced. */
    saved_pos = mmce_fs_lseek64(&backing, 0, SEEK_CUR);
    if (saved_pos < 0)
        return -EIO;

    want = mmce_fs_active_pos[slot];
    if (mmce_fs_lseek64(&backing, want, SEEK_SET) != want)
        return -EIO;

    result = mmce_fs_read(&backing, ptr, size);
    restore = mmce_fs_lseek64(&backing, saved_pos, SEEK_SET);
    if (restore != saved_pos)
        return -EIO;

    if (result > 0)
        mmce_fs_active_pos[slot] += result;
    return result;
}

static int *mmce_fs_find_free_handle(void) {
    for (int i = 0; i < MMCE_FS_MAX_FD; i++)
    {
        if (mmce_fs_fds[i] < 0)
            return &mmce_fs_fds[i];
    }
    return NULL;
}

//Check last used port, update if needed
static int mmce_fs_update_unit(int unit)
{
    int port = 2;

    if (unit != last_unit) {
        if (unit == 0)
            port = 2;
        else if (unit == 1)
            port = 3;

        last_unit = unit;

        DPRINTF("Unit changed, unit: %i port: %i\n", unit, port);
        mmce_sio2_set_port(port);
    }

    return 0;
}

int mmce_fs_init(iomanX_iop_device_t *f)
{
    DPRINTF("%s: init\n", __func__);
    memset(mmce_fs_fds, -1, sizeof(mmce_fs_fds));
    memset(mmce_fs_active_fd, -1, sizeof(mmce_fs_active_fd));
    memset(mmce_fs_active_pos, 0, sizeof(mmce_fs_active_pos));
    memset(mmce_fs_active_size, 0, sizeof(mmce_fs_active_size));
    return 0;
}

int mmce_fs_deinit(iomanX_iop_device_t *f)
{
    return 0;
}

int mmce_fs_open(iomanX_iop_file_t *file, const char *name, int flags, int mode)
{
    int res;

    u8 wrbuf[0x5];
    u8 rdbuf[0x3];

    /* Reject unrepresentable firmware paths before a single SIO2 command.
     * The synthetic active-card handle has one canonical read-only path. */
    if (!mmce_fs_wire_name_ok(name)) {
        return -ENAMETOOLONG;
    }
    if (mmce_fs_reserved_component(name) && !mmce_fs_is_active_name(name))
        return -EROFS;

    DPRINTF("%s unit: %i name: %s flags: 0x%x\n", __func__, file->unit, name, flags);

    //Update SIO2 port if unit changed ex mmce0: -> mmce1:
    mmce_fs_update_unit(file->unit);

    //Make sure theres file handles available
    file->privdata = (int*)mmce_fs_find_free_handle();

    if (file->privdata == NULL) {
        DPRINTF("%s ERROR: No free file handles available\n", __func__);
        return -1;
    }

    if (mmce_fs_is_active_name(name)) {
        int slot = mmce_fs_slot_index(file);
        int active_fd;
        s64 active_size;

        if ((flags & 3) != FIO_O_RDONLY || (flags & 0xF00) != 0) {
            file->privdata = NULL;
            return -EROFS;
        }

        res = mmce_fs_discover_active_fd(file, &active_fd, &active_size);
        if (res < 0) {
            file->privdata = NULL;
            return res;
        }

        *(int *)file->privdata = MMCE_FS_ACTIVE_HANDLE;
        mmce_fs_active_fd[slot] = active_fd;
        mmce_fs_active_pos[slot] = 0;
        mmce_fs_active_size[slot] = active_size;
        DPRINTF("%s: opened synthetic active card, backing fd %i\n", __func__, active_fd);
        return 0;
    }

    u8 packed_flags = 0;
    u8 filename_len = strlen(name) + 1;

    packed_flags  = (flags & 3) - 1;        //O_RDONLY, O_WRONLY, O_RDWR
    packed_flags |= (flags & 0x100) >> 5;   //O_APPEND
    packed_flags |= (flags & 0xE00) >> 4;   //O_CREATE, O_TRUNC, O_EXCL

    wrbuf[0x0] = MMCE_ID;                //Identifier
    wrbuf[0x1] = MMCE_CMD_FS_OPEN;       //Command
    wrbuf[0x2] = MMCE_RESERVED;          //Reserved
    wrbuf[0x3] = packed_flags;           //8 bit packed flags
    wrbuf[0x4] = 0xff;

    mmce_sio2_lock(); //Lock SIO2 for transfer

    //Packet #1: Command and flags
    res = mmce_sio2_tx_rx_pio(0x5, 0x2, wrbuf, rdbuf, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    if (rdbuf[0x1] != MMCE_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCE_REPLY_CONST);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #2: Filename
    res = mmce_sio2_tx_rx_pio(filename_len, 0x0, name, NULL, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #3 - File descriptor
    res = mmce_sio2_tx_rx_pio(0x0, 0x3, wrbuf, rdbuf, &timeout_1s);
    mmce_sio2_unlock();
    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] < MMCE_FS_MAX_FD) {
        *(int*)file->privdata = rdbuf[0x1];
        DPRINTF("%s Opened fd: %i\n", __func__, rdbuf[0x1]);
    } else {
        DPRINTF("%s ERROR: Got bad fd: %i\n", __func__, rdbuf[0x1]);
        file->privdata = NULL;
        /* 0xff is the card itself saying "no such file": report -ENOENT so
         * callers can tell absence from a timeout or garbled reply, which
         * stay -1. Ported from RiptOPL modules/mmce/mmceman. */
        return rdbuf[0x1] == 0xff ? -ENOENT : -1;
    }

    return 0;
}

int mmce_fs_close(iomanX_iop_file_t *file)
{
    int res = 0;

    u8 wrbuf[0x6];
    u8 rdbuf[0x6];

    DPRINTF("%s fd: %i\n", __func__, (u8)*(int*)file->privdata);

    mmce_fs_update_unit(file->unit);

    //Synthetic/reserved handles never own the firmware backing descriptor.
    if (*(int*)file->privdata >= 250) {
        int slot = mmce_fs_slot_index(file);
        if (slot >= 0) {
            mmce_fs_active_fd[slot] = -1;
            mmce_fs_active_pos[slot] = 0;
            mmce_fs_active_size[slot] = 0;
            mmce_fs_fds[slot] = -1;
        }
        file->privdata = NULL;
        return 0;
    }

    wrbuf[0x0] = MMCE_ID;                   //Identifier
    wrbuf[0x1] = MMCE_CMD_FS_CLOSE;         //Command
    wrbuf[0x2] = MMCE_RESERVED;             //Reserved
    wrbuf[0x3] = (u8)*(int*)file->privdata; //File descriptor
    wrbuf[0x4] = 0xff;                      //Padding
    wrbuf[0x5] = 0xff;                      //Termination byte

    //Packet #1: Command, file descriptor, return value
    mmce_sio2_lock();
    res = mmce_sio2_tx_rx_pio(sizeof(wrbuf), sizeof(rdbuf), wrbuf, rdbuf, &timeout_1s);
    mmce_sio2_unlock();

    /* The handle is torn down whatever the card answers, so its slot goes
     * back to the 16-entry pool on every exit. Before this, a timed-out or
     * refused close stranded the slot until reboot, and after enough of them
     * every mmceN: open/dopen failed. Only the slot value is reset; privdata
     * stays a valid pointer. Ported from RiptOPL modules/mmce/mmceman. */
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        *(int*)file->privdata = -1;
        return -1;
    }

    if (rdbuf[0x1] != MMCE_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCE_REPLY_CONST);
        *(int*)file->privdata = -1;
        return -1;
    }

    if (rdbuf[0x4] != 0x0) {
        res = rdbuf[0x4];
        DPRINTF("%s ERROR: got return value %i\n", __func__, res);
    }
    *(int*)file->privdata = -1;

    return res;
}

int mmce_fs_read(iomanX_iop_file_t *file, void *ptr, int size)
{
    int res;

    if (file->privdata != NULL && *(int *)file->privdata == MMCE_FS_ACTIVE_HANDLE)
        return mmce_fs_active_read(file, ptr, size);
    int bytes_read;

    u8 wrbuf[0xA];
    u8 rdbuf[0xA];

    DPRINTF("%s fd: %i, size: %i\n", __func__, (u8)*(int*)file->privdata, size);

    mmce_fs_update_unit(file->unit);

    wrbuf[0x0] = MMCE_ID;                   //Identifier
    wrbuf[0x1] = MMCE_CMD_FS_READ;          //Command
    wrbuf[0x2] = MMCE_RESERVED;             //Reserved
    wrbuf[0x3] = 0x0;                       //Transfer mode (unused)
    wrbuf[0x4] = (u8)*(int*)file->privdata; //File Descriptor
    wrbuf[0x5] = (size & 0xFF000000) >> 24; //Size
    wrbuf[0x6] = (size & 0x00FF0000) >> 16;
    wrbuf[0x7] = (size & 0x0000FF00) >> 8;
    wrbuf[0x8] = (size & 0x000000FF);
    wrbuf[0x9] = 0xff;

    mmce_sio2_lock();

    //Packet #1: Command, file descriptor, and size
    res = mmce_sio2_tx_rx_pio(0xA, 0xA, wrbuf, rdbuf, &timeout_2s);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    if (rdbuf[0x1] != MMCE_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCE_REPLY_CONST);
        mmce_sio2_unlock();
        return -1;
    }

    if (rdbuf[0x9] != 0x0) {
        DPRINTF("%s ERROR: P1 - Got bad return value from card, res %i\n", __func__, rdbuf[0x9]);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #2 - n: Raw read data
    res = mmce_sio2_rx_mixed(ptr, size);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #n + 1: Bytes read
    res = mmce_sio2_tx_rx_pio(0x0, 0x6, wrbuf, rdbuf, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    mmce_sio2_unlock();

    bytes_read  = rdbuf[0x1] << 24;
    bytes_read |= rdbuf[0x2] << 16;
    bytes_read |= rdbuf[0x3] << 8;
    bytes_read |= rdbuf[0x4];

    if (bytes_read != size) {
        DPRINTF("%s WARN: bytes read: %i, expected: %i\n", __func__, bytes_read, size);

        if (bytes_read > size)
            bytes_read = size;

    }

    return bytes_read;
}

int mmce_fs_write(iomanX_iop_file_t *file, void *ptr, int size)
{
    int res;

    if (file->privdata != NULL && *(int *)file->privdata == MMCE_FS_ACTIVE_HANDLE)
        return -EROFS;
    int bytes_written;

    u8 wrbuf[0xA];
    u8 rdbuf[0xA];

    DPRINTF("%s fd: %i, size: %i\n", __func__, (u8)*(int*)file->privdata, size);

    mmce_fs_update_unit(file->unit);

    wrbuf[0x0] = MMCE_ID;                   //Identifier
    wrbuf[0x1] = MMCE_CMD_FS_WRITE;         //Command
    wrbuf[0x2] = MMCE_RESERVED;             //Reserved
    wrbuf[0x3] = 0x0;                       //Transfer mode (unused)
    wrbuf[0x4] = (u8)*(int*)file->privdata; //File Descriptor
    wrbuf[0x5] = (size & 0xFF000000) >> 24;
    wrbuf[0x6] = (size & 0x00FF0000) >> 16;
    wrbuf[0x7] = (size & 0x0000FF00) >> 8;
    wrbuf[0x8] = (size & 0x000000FF);
    wrbuf[0x9] = 0xff;

    mmce_sio2_lock();

    //Packet #1: Command, file descriptor, and size
    res = mmce_sio2_tx_rx_pio(0xA, 0xA, wrbuf, rdbuf, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    if (rdbuf[0x1] != MMCE_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCE_REPLY_CONST);
        mmce_sio2_unlock();
        return -1;
    }

    if (rdbuf[0x9] != 0x0) {
        DPRINTF("%s ERROR: P1 - Got bad return value from card 0x%x\n", __func__, rdbuf[0x9]);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #2 - n: Raw write data
    res = mmce_sio2_tx_mixed(ptr, size);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    //Packets #n + 1: Bytes written
    res = mmce_sio2_tx_rx_pio(0x0, 0x6, wrbuf, rdbuf, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    mmce_sio2_unlock();

    bytes_written  = rdbuf[0x1] << 24;
    bytes_written |= rdbuf[0x2] << 16;
    bytes_written |= rdbuf[0x3] << 8;
    bytes_written |= rdbuf[0x4];

    if (bytes_written != size) {
        DPRINTF("%s bytes written: %i, expected: %i\n", __func__, bytes_written, size);
    }

    return bytes_written;
}

int mmce_fs_lseek(iomanX_iop_file_t *file, int offset, int whence)
{
    int res;
    int position = -1;

    if (file->privdata != NULL && *(int *)file->privdata == MMCE_FS_ACTIVE_HANDLE) {
        s64 active = mmce_fs_active_seek(file, offset, whence);
        return active < 0 ? (int)active : (int)active;
    }

    u8 wrbuf[0x9];
    u8 rdbuf[0xe];

    DPRINTF("%s fd: %i, offset: %i, whence: %i\n", __func__, (u8)*(int*)file->privdata, offset, whence);

    mmce_fs_update_unit(file->unit);

    wrbuf[0x0] = MMCE_ID;                       //Identifier
    wrbuf[0x1] = MMCE_CMD_FS_LSEEK;             //Command
    wrbuf[0x2] = MMCE_RESERVED;                 //Reserved
    wrbuf[0x3] = (u8)(*(int*)file->privdata);   //File descriptor
    wrbuf[0x4] = (offset & 0xFF000000) >> 24;   //Offset
    wrbuf[0x5] = (offset & 0x00FF0000) >> 16;
    wrbuf[0x6] = (offset & 0x0000FF00) >> 8;
    wrbuf[0x7] = (offset & 0x000000FF);
    wrbuf[0x8] = (u8)(whence);                  //Whence

    //Packet #1: Command, file descriptor, offset, and whence
    mmce_sio2_lock();
    res = mmce_sio2_tx_rx_pio(0x9, 0xe, wrbuf, rdbuf, &timeout_1s);
    mmce_sio2_unlock();

    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != MMCE_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCE_REPLY_CONST);
        return -1;
    }

    position  = rdbuf[0x9] << 24;
    position |= rdbuf[0xa] << 16;
    position |= rdbuf[0xb] << 8;
    position |= rdbuf[0xc];

    DPRINTF("%s position %i\n", __func__, position);

    return position;
}

int mmce_fs_ioctl(iomanX_iop_file_t *file, int cmd, void *data)
{
    return 0;
}

/*Note: Due to a bug in FILEIO, mkdir will be called after remove unless
        sbv_patch_fileio is used. See ps2sdk/ee/sbv/src/patch_fileio.c */
int mmce_fs_remove(iomanX_iop_file_t *file, const char *name)
{
    int res;

    u8 wrbuf[0x4];
    u8 rdbuf[0x3];

    if (!mmce_fs_wire_name_ok(name)) {
        return -ENAMETOOLONG;
    }
    if (mmce_fs_reserved_component(name))
        return -EROFS;
    DPRINTF("%s name: %s\n", __func__, name);

    mmce_fs_update_unit(file->unit);

    u8 filename_len = strlen(name) + 1;

    wrbuf[0x0] = MMCE_ID;            //Identifier
    wrbuf[0x1] = MMCE_CMD_FS_REMOVE; //Command
    wrbuf[0x2] = MMCE_RESERVED;      //Reserved
    wrbuf[0x3] = 0xff;

    mmce_sio2_lock();

    //Packet #1: Command
    res = mmce_sio2_tx_rx_pio(0x4, 0x2, wrbuf, rdbuf, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    if (rdbuf[0x1] != MMCE_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCE_REPLY_CONST);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #2: Filename
    res = mmce_sio2_tx_rx_pio(filename_len, 0x0, name, NULL, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #3: Return value
    res = mmce_sio2_tx_rx_pio(0x0, 0x3, NULL, rdbuf, &timeout_1s);
    mmce_sio2_unlock();

    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != 0x0) {
        DPRINTF("%s ERROR: Card failed to remove %s, return value %i\n", __func__, name, rdbuf[0x1]);
        return -1;
    }

    return 0;
}

int mmce_fs_mkdir(iomanX_iop_file_t *file, const char *name, int flags)
{
    int res;

    u8 wrbuf[0x4];
    u8 rdbuf[0x3];

    if (!mmce_fs_wire_name_ok(name)) {
        return -ENAMETOOLONG;
    }
    if (mmce_fs_reserved_component(name))
        return -EROFS;
    DPRINTF("%s name: %s\n", __func__, name);

    mmce_fs_update_unit(file->unit);

    u8 dir_len = strlen(name) + 1;

    wrbuf[0x0] = MMCE_ID;            //Identifier
    wrbuf[0x1] = MMCE_CMD_FS_MKDIR;  //Command
    wrbuf[0x2] = MMCE_RESERVED;      //Reserved
    wrbuf[0x3] = 0xff;

    mmce_sio2_lock();

    //Packet #1: Command
    res = mmce_sio2_tx_rx_pio(0x4, 0x2, wrbuf, rdbuf, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    if (rdbuf[0x1] != MMCE_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCE_REPLY_CONST);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #2: Dirname
    res = mmce_sio2_tx_rx_pio(dir_len, 0x0, name, NULL, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #3: Return value
    res = mmce_sio2_tx_rx_pio(0x0, 0x3, NULL, rdbuf, &timeout_1s);
    mmce_sio2_unlock();

    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != 0x0) {
        DPRINTF("%s ERROR: Card failed to mkdir %s, return value %i\n", __func__, name, rdbuf[0x1]);
        return -1;
    }

    return 0;
}

int mmce_fs_rmdir(iomanX_iop_file_t *file, const char *name)
{
    int res;

    u8 wrbuf[0x4];
    u8 rdbuf[0x3];

    if (!mmce_fs_wire_name_ok(name)) {
        return -ENAMETOOLONG;
    }
    if (mmce_fs_reserved_component(name))
        return -EROFS;
    DPRINTF("%s name: %s\n", __func__, name);

    mmce_fs_update_unit(file->unit);

    u8 dir_len = strlen(name) + 1;

    wrbuf[0x0] = MMCE_ID;            //Identifier
    wrbuf[0x1] = MMCE_CMD_FS_RMDIR;  //Command
    wrbuf[0x2] = MMCE_RESERVED;      //Reserved
    wrbuf[0x3] = 0xff;

    mmce_sio2_lock();

    //Packet #1: Command
    res = mmce_sio2_tx_rx_pio(0x4, 0x2, wrbuf, rdbuf, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    if (rdbuf[0x1] != MMCE_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCE_REPLY_CONST);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #2: Dirname
    res = mmce_sio2_tx_rx_pio(dir_len, 0x0, name, NULL, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #3: Return value
    res = mmce_sio2_tx_rx_pio(0x0, 0x3, NULL, rdbuf, &timeout_1s);
    mmce_sio2_unlock();

    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != 0x0) {
        DPRINTF("%s ERROR: Card failed to rmdir %s, return value %i\n", __func__, name, rdbuf[0x1]);
        return -1;
    }

    return 0;
}

int mmce_fs_dopen(iomanX_iop_file_t *file, const char *name)
{
    int res;

    u8 wrbuf[0x5];
    u8 rdbuf[0x3];

    /* Firmware uses a one-byte pathname length; fail before narrowing or I/O. */
    if (!mmce_fs_wire_name_ok(name)) {
        return -ENAMETOOLONG;
    }
    DPRINTF("%s name: %s\n", __func__, name);

    mmce_fs_update_unit(file->unit);

    file->privdata = (int*)mmce_fs_find_free_handle();

    if (file->privdata == NULL) {
        DPRINTF("%s ERROR: No free file handles available\n", __func__);
        return -1;
    }

    u8 dir_len = strlen(name) + 1;

    wrbuf[0x0] = MMCE_ID;            //Identifier
    wrbuf[0x1] = MMCE_CMD_FS_DOPEN;  //Command
    wrbuf[0x2] = MMCE_RESERVED;      //Reserved
    wrbuf[0x3] = 0xff;

    mmce_sio2_lock();

    //Packet #1: Command
    res = mmce_sio2_tx_rx_pio(0x4, 0x2, wrbuf, rdbuf, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    if (rdbuf[0x1] != MMCE_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCE_REPLY_CONST);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #2: Dirname
    res = mmce_sio2_tx_rx_pio(dir_len, 0x0, name, NULL, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #n + 1: File descriptor
    res = mmce_sio2_tx_rx_pio(0x0, 0x3, NULL, rdbuf, &timeout_1s);
    mmce_sio2_unlock();

    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != 0xff) {
        *(int*)file->privdata = rdbuf[0x1];
    } else {
        /* The card answered "no such directory": -ENOENT, as in open. */
        DPRINTF("%s ERROR: Got invalid fd: %i\n", __func__, rdbuf[0x1]);
        file->privdata = NULL;
        return -ENOENT;
    }

    return 0;
}

int mmce_fs_dclose(iomanX_iop_file_t *file)
{
    int res;

    u8 wrbuf[0x5];
    u8 rdbuf[0x6];

    mmce_fs_update_unit(file->unit);

    wrbuf[0x0] = MMCE_ID;                   //Identifier
    wrbuf[0x1] = MMCE_CMD_FS_DCLOSE;        //Command
    wrbuf[0x2] = MMCE_RESERVED;             //Reserved
    wrbuf[0x3] = (u8)*(int*)file->privdata; //File descriptor

    DPRINTF("%s fd: %i\n", __func__, (u8)*(int*)file->privdata);

    //Packet #1: Command and file descriptor
    mmce_sio2_lock();
    res = mmce_sio2_tx_rx_pio(0x4, 0x6, wrbuf, rdbuf, &timeout_1s);
    mmce_sio2_unlock();

    /* Release the slot on every exit, as in mmce_fs_close. The pointer is
     * cleared only on the clean path, as before. */
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        *(int*)file->privdata = -1;
        return -1;
    }

    if (rdbuf[0x1] != MMCE_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCE_REPLY_CONST);
        *(int*)file->privdata = -1;
        return -1;
    }

    if (rdbuf[0x4] != 0x0) {
        DPRINTF("%s ERROR: Card failed to close dir %i, return value %i\n", __func__, (u8)*(int*)file->privdata, rdbuf[0x4]);
        *(int*)file->privdata = -1;
        return -1;
    }

    *(int*)file->privdata = -1;
    file->privdata = NULL;

    return 0;
}

int mmce_fs_dread(iomanX_iop_file_t *file, iox_dirent_t *dirent)
{
    int res;

    u8 wrbuf[0x5];
    u8 rdbuf[0x2B];

    DPRINTF("%s fd: %i\n", __func__, (u8)*(int*)file->privdata);

    mmce_fs_update_unit(file->unit);

    u8 filename_len = 0;

    wrbuf[0x0] = MMCE_ID;                   //Identifier
    wrbuf[0x1] = MMCE_CMD_FS_DREAD;         //Command
    wrbuf[0x2] = MMCE_RESERVED;             //Reserved
    wrbuf[0x3] = (u8)*(int*)file->privdata; //File descriptor
    wrbuf[0x4] = 0xff;

    DPRINTF("%s fd: %i\n", __func__, (u8)*(int*)file->privdata);

    mmce_sio2_lock();

    //Packet #1: Command and file descriptor
    res = mmce_sio2_tx_rx_pio(0x5, 0x5, wrbuf, rdbuf, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    if (rdbuf[0x1] != MMCE_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCE_REPLY_CONST);
        mmce_sio2_unlock();
        return -1;
    }

    if (rdbuf[0x4] != 0) {
        DPRINTF("%s ERROR: Returned %i, Expected 0\n", __func__, rdbuf[0x4]);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #n + 1: io_stat_t and filename len
    res = mmce_sio2_tx_rx_pio(0x0, 0x2A, NULL, rdbuf, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    //Could cast but not sure about alignment?
    dirent->stat.mode  = rdbuf[0x1] << 24;
    dirent->stat.mode |= rdbuf[0x2] << 16;
    dirent->stat.mode |= rdbuf[0x3] << 8;
    dirent->stat.mode |= rdbuf[0x4];

    dirent->stat.attr  = rdbuf[0x5] << 24;
    dirent->stat.attr |= rdbuf[0x6] << 16;
    dirent->stat.attr |= rdbuf[0x7] << 8;
    dirent->stat.attr |= rdbuf[0x8];

    dirent->stat.size  = rdbuf[0x9] << 24;
    dirent->stat.size |= rdbuf[0xA] << 16;
    dirent->stat.size |= rdbuf[0xB] << 8;
    dirent->stat.size |= rdbuf[0xC];

    dirent->stat.ctime[0] = rdbuf[0xD];
    dirent->stat.ctime[1] = rdbuf[0xE];
    dirent->stat.ctime[2] = rdbuf[0xF];
    dirent->stat.ctime[3] = rdbuf[0x10];
    dirent->stat.ctime[4] = rdbuf[0x11];
    dirent->stat.ctime[5] = rdbuf[0x12];
    dirent->stat.ctime[6] = rdbuf[0x13];
    dirent->stat.ctime[7] = rdbuf[0x14];

    dirent->stat.atime[0] = rdbuf[0x15];
    dirent->stat.atime[1] = rdbuf[0x16];
    dirent->stat.atime[2] = rdbuf[0x17];
    dirent->stat.atime[3] = rdbuf[0x18];
    dirent->stat.atime[4] = rdbuf[0x19];
    dirent->stat.atime[5] = rdbuf[0x1A];
    dirent->stat.atime[6] = rdbuf[0x1B];
    dirent->stat.atime[7] = rdbuf[0x1C];

    dirent->stat.mtime[0] = rdbuf[0x1D];
    dirent->stat.mtime[1] = rdbuf[0x1E];
    dirent->stat.mtime[2] = rdbuf[0x1F];
    dirent->stat.mtime[3] = rdbuf[0x20];
    dirent->stat.mtime[4] = rdbuf[0x21];
    dirent->stat.mtime[5] = rdbuf[0x22];
    dirent->stat.mtime[6] = rdbuf[0x23];
    dirent->stat.mtime[7] = rdbuf[0x24];

    dirent->stat.hisize  = rdbuf[0x25] << 24;
    dirent->stat.hisize |= rdbuf[0x26] << 16;
    dirent->stat.hisize |= rdbuf[0x27] << 8;
    dirent->stat.hisize |= rdbuf[0x28];

    filename_len = rdbuf[0x29];

    //Packet #n + 2: Filename
    res = mmce_sio2_tx_rx_pio(0x0, filename_len, NULL, dirent->name, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P4 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #n + 3: Padding, resevered, and term
    res = mmce_sio2_tx_rx_pio(0x0, 0x3, NULL, rdbuf, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P5 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    mmce_sio2_unlock();

    return rdbuf[0x1]; //itr fd from mmce
}

int mmce_fs_getstat(iomanX_iop_file_t *file, const char *name, iox_stat_t *stat)
{
    int res;

    u8 wrbuf[0xFF];
    u8 rdbuf[0x2B];

    /* The MMCE wire protocol cannot represent 255+ byte pathnames. */
    if (!mmce_fs_wire_name_ok(name)) {
        return -ENAMETOOLONG;
    }
    u8 len = strlen(name) + 1;

    DPRINTF("%s filename: %s\n", __func__, name);

    mmce_fs_update_unit(file->unit);

    wrbuf[0x0] = MMCE_ID;               //Identifier
    wrbuf[0x1] = MMCE_CMD_FS_GETSTAT;   //Command
    wrbuf[0x2] = MMCE_RESERVED;         //Reserved
    wrbuf[0x3] = 0xff;                  //Padding

    mmce_sio2_lock();

    //Packet #1: Command and padding
    res = mmce_sio2_tx_rx_pio(0x4, 0x4, wrbuf, rdbuf, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    if (rdbuf[0x1] != MMCE_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCE_REPLY_CONST);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #2: Filename
    res = mmce_sio2_tx_rx_pio(len, 0x0, name, NULL, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #n + 1: io_stat_t and term
    res = mmce_sio2_tx_rx_pio(0x0, 0x2b, NULL, rdbuf, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P4 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    mmce_sio2_unlock();

    /* Firmware GETSTAT returns 1 when its read-only path open fails.
     * Translate that completed response to fileXio's missing-path errno;
     * transport failures above must remain errors, not permission to create.
     * Do not decode stale stat fields from a failed firmware response.
     * From saildot4k/Launchstation df7214d. */
    if (rdbuf[0x29] != 0)
        return rdbuf[0x29] == 1 ? -ENOENT : -EIO;

    //Could cast but not sure about alignment?
    stat->mode  = rdbuf[0x1] << 24;
    stat->mode |= rdbuf[0x2] << 16;
    stat->mode |= rdbuf[0x3] << 8;
    stat->mode |= rdbuf[0x4];

    stat->attr  = rdbuf[0x5] << 24;
    stat->attr |= rdbuf[0x6] << 16;
    stat->attr |= rdbuf[0x7] << 8;
    stat->attr |= rdbuf[0x8];

    stat->size  = rdbuf[0x9] << 24;
    stat->size |= rdbuf[0xA] << 16;
    stat->size |= rdbuf[0xB] << 8;
    stat->size |= rdbuf[0xC];

    stat->ctime[0] = rdbuf[0xD];
    stat->ctime[1] = rdbuf[0xE];
    stat->ctime[2] = rdbuf[0xF];
    stat->ctime[3] = rdbuf[0x10];
    stat->ctime[4] = rdbuf[0x11];
    stat->ctime[5] = rdbuf[0x12];
    stat->ctime[6] = rdbuf[0x13];
    stat->ctime[7] = rdbuf[0x14];

    stat->atime[0] = rdbuf[0x15];
    stat->atime[1] = rdbuf[0x16];
    stat->atime[2] = rdbuf[0x17];
    stat->atime[3] = rdbuf[0x18];
    stat->atime[4] = rdbuf[0x19];
    stat->atime[5] = rdbuf[0x1A];
    stat->atime[6] = rdbuf[0x1B];
    stat->atime[7] = rdbuf[0x1C];

    stat->mtime[0] = rdbuf[0x1D];
    stat->mtime[1] = rdbuf[0x1E];
    stat->mtime[2] = rdbuf[0x1F];
    stat->mtime[3] = rdbuf[0x20];
    stat->mtime[4] = rdbuf[0x21];
    stat->mtime[5] = rdbuf[0x22];
    stat->mtime[6] = rdbuf[0x23];
    stat->mtime[7] = rdbuf[0x24];

    stat->hisize  = rdbuf[0x25] << 24;
    stat->hisize |= rdbuf[0x26] << 16;
    stat->hisize |= rdbuf[0x27] << 8;
    stat->hisize |= rdbuf[0x28];

    return 0;
}

int	mmce_fs_rename(iomanX_iop_file_t *file, const char *old_name, const char *new_name)
{
    int res;

    u8 wrbuf[0x4];
    u8 rdbuf[0x3];

    if (!mmce_fs_wire_name_ok(old_name) || !mmce_fs_wire_name_ok(new_name))
        return -ENAMETOOLONG;
    if (mmce_fs_reserved_component(old_name) ||
        mmce_fs_reserved_component(new_name))
        return -EROFS;

    DPRINTF("%s unit: %i old_name: %s new_name: %s\n", __func__, file->unit, old_name, new_name);

    //Update SIO2 port if unit changed ex mmce0: -> mmce1:
    mmce_fs_update_unit(file->unit);

    u8 old_filename_len = strlen(old_name) + 1;
    u8 new_filename_len = strlen(new_name) + 1;

    wrbuf[0x0] = MMCE_ID;                //Identifier
    wrbuf[0x1] = MMCE_CMD_FS_RENAME;     //Command
    wrbuf[0x2] = MMCE_RESERVED;          //Reserved
    wrbuf[0x3] = 0xff;

    mmce_sio2_lock(); //Lock SIO2 for transfer

    //Packet #1: Command and flags
    res = mmce_sio2_tx_rx_pio(sizeof(wrbuf), 0x2, wrbuf, rdbuf, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    if (rdbuf[0x1] != MMCE_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCE_REPLY_CONST);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #2: Old Filename
    res = mmce_sio2_tx_rx_pio(old_filename_len, 0x0, old_name, NULL, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P2 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #3: New Filename
    res = mmce_sio2_tx_rx_pio(new_filename_len, 0x0, new_name, NULL, &timeout_1s);
    if (res == -1) {
        DPRINTF("%s ERROR: P3 - Timedout waiting for /ACK\n", __func__);
        mmce_sio2_unlock();
        return -1;
    }

    //Packet #4: Return value
    res = mmce_sio2_tx_rx_pio(0x0, sizeof(rdbuf), NULL, rdbuf, &timeout_1s);
    mmce_sio2_unlock();
    if (res == -1) {
        DPRINTF("%s ERROR: P4 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != 0x0) {
        DPRINTF("%s ERROR: Card failed to rename %s to %s, return value %i\n",
                __func__, old_name, new_name, rdbuf[0x1]);
        return -1;
    }

    return 0;
}


s64 mmce_fs_lseek64(iomanX_iop_file_t *file, s64 offset, int whence)
{
    int res;
    s64 position = -1;

    if (file->privdata != NULL && *(int *)file->privdata == MMCE_FS_ACTIVE_HANDLE)
        return mmce_fs_active_seek(file, offset, whence);

    u8 wrbuf[0xd];
    u8 rdbuf[0x16];

    DPRINTF("%s fd: %i, whence: %i\n", __func__, (u8)*(int*)file->privdata, whence);

    mmce_fs_update_unit(file->unit);

    wrbuf[0x0] = MMCE_ID;                       //Identifier
    wrbuf[0x1] = MMCE_CMD_FS_LSEEK64;           //Command
    wrbuf[0x2] = MMCE_RESERVED;                 //Reserved
    wrbuf[0x3] = (u8)(*(int*)file->privdata);   //File descriptor

    wrbuf[0x4] = (offset & 0xFF00000000000000) >> 56;   //Offset
    wrbuf[0x5] = (offset & 0x00FF000000000000) >> 48;
    wrbuf[0x6] = (offset & 0x0000FF0000000000) >> 40;
    wrbuf[0x7] = (offset & 0x000000FF00000000) >> 32;
    wrbuf[0x8] = (offset & 0x00000000FF000000) >> 24;
    wrbuf[0x9] = (offset & 0x0000000000FF0000) >> 16;
    wrbuf[0xa] = (offset & 0x000000000000FF00) >> 8;
    wrbuf[0xb] = (offset & 0x00000000000000FF);
    wrbuf[0xc] = (u8)(whence);  //Whence

    //Packet #1: Command, file descriptor, offset, and whence
    mmce_sio2_lock();
    res = mmce_sio2_tx_rx_pio(0xd, 0x16, wrbuf, rdbuf, &timeout_1s);
    mmce_sio2_unlock();
    if (res == -1) {
        DPRINTF("%s ERROR: P1 - Timedout waiting for /ACK\n", __func__);
        return -1;
    }

    if (rdbuf[0x1] != MMCE_REPLY_CONST) {
        DPRINTF("%s ERROR: Invalid response from card. Got 0x%x, Expected 0x%x\n", __func__, rdbuf[0x1], MMCE_REPLY_CONST);
        return -1;
    }

    position  = (s64)rdbuf[0xd] << 56;
    position |= (s64)rdbuf[0xe] << 48;
    position |= (s64)rdbuf[0xf] << 40;
    position |= (s64)rdbuf[0x10] << 32;
    position |= (s64)rdbuf[0x11] << 24;
    position |= (s64)rdbuf[0x12] << 16;
    position |= (s64)rdbuf[0x13] << 8;
    position |= (s64)rdbuf[0x14];

    return position;
}

int mmce_fs_devctl(iomanX_iop_file_t *fd, const char *name, int cmd, void *arg, unsigned int arglen, void *buf, unsigned int buflen)
{
    int res = 0;

    int type;
    int mode;
    int num;

    int args;

    DPRINTF("%s cmd: %i, arglen, buflen: %i\n", __func__, cmd, arglen, buflen);

    mmce_fs_update_unit(fd->unit);

    switch (cmd) {
        case MMCE_CMD_PING:
            res = mmce_cmd_ping();
        break;

        case MMCE_CMD_GET_STATUS:
            res = mmce_cmd_get_status();
        break;

        case MMCE_CMD_GET_CARD:
            res = mmce_cmd_get_card();
        break;

        case MMCE_CMD_SET_CARD:
            args = *(u32*)arg;
            type = (args & 0xff000000) >> 24;
            mode = (args & 0x00ff0000) >> 16;
            num  = (args & 0x0000ffff);

            res = mmce_cmd_set_card(type, mode, num);
        break;

        case MMCE_CMD_GET_CHANNEL:
            res = mmce_cmd_get_channel();
        break;

        case MMCE_CMD_SET_CHANNEL:
            args = *(u32*)arg;
            mode = (args & 0x00ff0000) >> 16;
            num  = (args & 0x0000ffff);
            res = mmce_cmd_set_channel(mode, num);
        break;

        case MMCE_CMD_GET_GAMEID:
            res = mmce_cmd_get_gameid(buf);
        break;

        case MMCE_CMD_SET_GAMEID:
            char *str = (char*)arg;
            res = mmce_cmd_set_gameid(str);
        break;

        case MMCE_CMD_RESET:
            res = mmce_cmd_reset();
        break;

        case MMCE_CMD_UNMOUNT_BOOTCARD:
            res = mmce_cmd_unmount_bootcard();
        break;

        case MMCE_CMD_SET_CARD_CHANNEL:
        {
            if (arg == NULL || arglen < 5) {
                DPRINTF("%s ERROR: Set card/channel requires 5 argument bytes, got %i\n",
                        __func__, arglen);
                res = -1;
                break;
            }

            u8 *args_ptr = (u8 *)arg;
            u8 type = args_ptr[0];
            u16 card = ((u16)args_ptr[1] << 8) | (u16)args_ptr[2];
            u16 chan = ((u16)args_ptr[3] << 8) | (u16)args_ptr[4];
            res = mmce_cmd_set_card_channel(type, card, chan);
        }
        break;

        case MMCE_SETTINGS_ACK_WAIT_CYCLES:
            args = *(u32*)arg;
            mmce_sio2_update_ack_wait_cycles(args);
        break;

        case MMCE_SETTINGS_SET_ALARMS:
            args = *(u32*)arg;
            mmce_sio2_set_use_alarm(args);
        break;

        default:
        break;
    }

    return res;
}

int mmce_fs_ioctl2(iomanX_iop_file_t *file, int cmd, void *arg, unsigned int arglen, void *data, unsigned int datalen)
{
    int res = 0;

    switch(cmd) {
        //TEMP: Used to get iop side fd of a file
        case MMCE_CMD_IOCTL_GET_FD:
            res = (u8)*(int*)file->privdata;
        break;
        default:
    }

    return res;
}

static iomanX_iop_device_ops_t mmce_fio_ops =
{
	&mmce_fs_init,    //init
	&mmce_fs_deinit,  //deinit
	NOT_SUPPORTED_OP, //format
	&mmce_fs_open,    //open
	&mmce_fs_close,   //close
	&mmce_fs_read,    //read
	&mmce_fs_write,   //write
	&mmce_fs_lseek,   //lseek
	&mmce_fs_ioctl,   //ioctl
	&mmce_fs_remove,  //remove
	&mmce_fs_mkdir,   //mkdir
	&mmce_fs_rmdir,   //rmdir
	&mmce_fs_dopen,   //dopen
	&mmce_fs_dclose,  //dclose
	&mmce_fs_dread,   //dread
	&mmce_fs_getstat, //getstat
	NOT_SUPPORTED_OP, //chstat
    //EXTENDED OPS
    &mmce_fs_rename, //rename
    NOT_SUPPORTED_OP, //chdir
    NOT_SUPPORTED_OP, //sync
    NOT_SUPPORTED_OP, //mount
    NOT_SUPPORTED_OP, //umount
    &mmce_fs_lseek64, //lseek64
    &mmce_fs_devctl,  //devctl
    NOT_SUPPORTED_OP, //symlink
    NOT_SUPPORTED_OP, //readlink
    &mmce_fs_ioctl2,  //ioctl2
};

static iomanX_iop_device_t mmce_dev =
{
	"mmce",
	(IOP_DT_FS | IOP_DT_FSEXT),
	1,
	"Filesystem access mmce",
	&mmce_fio_ops, //fill this with an instance of iomanX_iop_device_ops
};

int mmce_fs_register(void) {
    DPRINTF("Registering %s device\n", mmce_dev.name);
    iomanX_DelDrv(mmce_dev.name);

    if (iomanX_AddDrv(&mmce_dev)!= 0)
        return 0;

    return 1;
}

int mmce_fs_unregister(void) {
    iomanX_DelDrv(mmce_dev.name);
    return 0;
}
