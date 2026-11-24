/* Launchstation addition to bdmfs_fatfs (ORIGIN.md). Academic Free License 2.0. */
#include <errno.h>

#include "ff.h"
#include "bdmfs_reserve.h"

/* BDMFS_IOCTL2_RESERVE. Called under the filesystem lock. f_expand looks for the first free run of
 * clusters that holds the whole file, starting at the volume's allocation hint and wrapping, and
 * allocates it. The ordinary allocator takes the next free cluster instead, whatever follows it,
 * so a file written on a volume with holes left by deleted files is spread over those holes.
 * The directory entry keeps size 0 until the file is closed, as with ordinary writes. */
int fs_reserve_contiguous(FIL *file, unsigned long long bytes)
{
    FRESULT result;

    if ((FSIZE_t)bytes != bytes)
        return 0;
    result = f_expand(file, (FSIZE_t)bytes, 1);
    if (result == FR_OK)
        return BDMFS_RESERVE_OK;
    return result == FR_DENIED ? 0 : -EIO;
}
