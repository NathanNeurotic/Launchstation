/* Contiguous allocation request of Launchstation's copy of the FAT/exFAT driver
 * (iop/bdmfs_fatfs/ORIGIN.md). Shared by the EE and the driver. */
#ifndef BDMFS_RESERVE_H
#define BDMFS_RESERVE_H

/* ioctl2 on a file just created for writing and still empty. data: the final size in bytes, one
 * unsigned 64-bit value. The driver gives the file one unbroken run of clusters of that size, so
 * the writes that follow fill it in place.
 * Returns BDMFS_RESERVE_OK when the run was allocated. Returns 0 when it was not: the volume has no
 * free run that long, the size is 0 or does not fit the format (4 GB on FAT), or the driver is the
 * stock one, which answers 0 to requests it does not know. The file is then still empty and is
 * written the ordinary way. A negative errno reports a failed request or a disk error. */
#define BDMFS_IOCTL2_RESERVE 0x4C535201
#define BDMFS_RESERVE_OK     0x4C535201

#endif
