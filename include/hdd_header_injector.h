#ifndef HDD_HEADER_INJECTOR_H
#define HDD_HEADER_INJECTOR_H

#include <stddef.h>

#define HDD_HEADER_SOURCE_PARTITION_NAME_MAX 32

typedef struct
{
	char name[HDD_HEADER_SOURCE_PARTITION_NAME_MAX + 1];
} HddHeaderSourcePartition;

int HddHeaderListSourcePartitions(const char *source_device, HddHeaderSourcePartition *partitions, int max_partitions, int *invalid_count);
int HddPartitionHeaderSystemCnfExists(const char *partition_name);
int EditHddPartitionHeaderSystemCnf(const char *partition_name);
extern int (*hddHeaderTextEditor)(char *path); /* TextEditor by default */
int InjectHddPartitionHeaderFromSource(const char *partition_name, const char *source_device, char *source_dir, size_t source_dir_size,
                                       int *pfs_files_copied, int *pfs_copy_error);
/* Fresh PFS launcher: generate PATINFO metadata around a downloaded SAS icon.
 * The caller owns the unmounted partition and validates the ELF target. */
int InjectHddAppLauncher(const char *partition_name, const char *elf_path,
                         const char *title, const void *icon, unsigned icon_size);
int InjectHddAppLauncherWithArg(const char *partition_name, const char *elf_path,
                                const char *title, const void *icon, unsigned icon_size, const char *argument);

/* HDD PS2X metadata and separate list/delete models, supplied by the DP asset
 * pipeline. Memory-card icon.sys is a different format and is rejected. */
int InjectHddAppAssets(const char *partition_name, const char *cnf, const char *settings,
                       const void *list, unsigned list_size, const void *del, unsigned del_size);

#endif
