#ifndef PSU_FUNCTIONS_H
#define PSU_FUNCTIONS_H

#include "launchelf.h"
#include "psu_types.h"

void clear_psu_header(psu_header *psu);
void pad_psu_header(psu_header *psu);
/* Whether 'path' holds a file that is not a PSU backup of the save folder
 * 'folder_stats': another save's backup, or no PSU at all. */
int psu_backup_holds_other_save(const char *path, const mcT_header *folder_stats);
/* A backup to the .psu 'path' writes a temporary file beside it (out_fd).
 * Finalize closes it and only then replaces an older .psu with it; discard
 * closes and removes it. Create and finalize return 0 or a negative error.
 * A finalize that fails after it removed the older .psu sets *kept: the
 * temporary file, complete, stays as the only backup of that name. */
int psu_backup_create_root_image(const char *path, const mcT_header *folder_stats, int *out_fd, int *psu_content);
int psu_backup_finalize_root_image(int out_fd, const char *path, const mcT_header *folder_stats, int psu_content, int *kept);
void psu_backup_discard_root_image(int out_fd, const char *path);
int psu_backup_begin_file_entry(int psu_fd, const mcT_header *file_stats, int *psu_pad_size, int *psu_content);
int psu_backup_write_padding(int psu_fd, int psu_pad_size);
int psu_restore_open_root_image(const char *path, FILEINFO *folder_file, int *in_fd, int *psu_content);
int psu_restore_read_file_entry(int psu_fd, FILEINFO *file, int *psu_pad_size);
/* Reads every entry header of the open .psu (of 'size' bytes, with
 * 'psu_content' entries) and returns 0, leaving it at its first entry, when
 * each is whole, none is a subfolder and no file is named outside the save
 * folder; otherwise a negative error. */
int psu_restore_check_entries(int psu_fd, int psu_content, s64 size);
int psu_restore_apply_entry_stats_to_mc(const char *out_dir, const sceMcTblGetDir *stats, int set_mode);

#endif
