#ifndef FILER_ACTIONS_H
#define FILER_ACTIONS_H

#include "launchelf.h"

u64 getFileSize(const char *path, const FILEINFO *file);
int filerCanSetCustomTimestamp(const char *path, const FILEINFO *file);
int filerCanOrganizeFolderTimestamps(const char *path);
int time_manip_custom(const char *path, const FILEINFO *file, char *_msg0);
int time_manip_organize(const char *path, const FILEINFO *file, char *_msg0);
void make_title_cfg(const char *path, const FILEINFO *file, char *_msg0);

/* Timestamp tools: optional replacement screens (NULL = classic). */
typedef struct
{
	FILEINFO *folders;          /* newest first; the edited one moves as its date changes */
	sceMcStDateTime *original;
	int count;
	sceMcStDateTime *timestamp; /* the edited folder's date */
	int reserve_tuna;
	char selected[MAX_NAME];    /* the folder being edited */
	int position;               /* field under the cursor, in display order */
} FilerDateEditor;

typedef struct
{
	int (*choose)(const char *title, const char **items, int count); /* -1 = back */
	void (*progress)(int done, int total, const char *folder);
	int (*edit)(FilerDateEditor *e); /* 1 = apply the dates, 0 = back */
} FilerTimestampUi;

extern const FilerTimestampUi *filerTimestampUi;
int filerDateEditorSync(FilerDateEditor *e); /* index of the edited folder */
int filerDateEditorSelect(FilerDateEditor *e, int direction);
int filerDateEditorMoveNextTo(FilerDateEditor *e, int newer);
void filerDateEditorAdjust(FilerDateEditor *e, int delta);
int filerDateEditorFieldCount(void);
void filerDateEditorField(const sceMcStDateTime *ts, int position, char *text, int size, const char **name, const char **sep);
void filerDateEditorFormat(const sceMcStDateTime *ts, char *out, int size);
int filerIsExploitProtectedPath(const char *path, const FILEINFO *file);
int filerConfirmExploitDelete(const char *path, const FILEINFO *file);
int filerConfirmExploitModify(const char *path, const FILEINFO *file);
int delete (const char *path, const FILEINFO *file);
int Rename(const char *path, const FILEINFO *file, const char *name);
int newdir(const char *path, const char *name);

#endif
