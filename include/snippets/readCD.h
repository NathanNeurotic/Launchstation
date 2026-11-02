#ifndef LIBCDVD_LEGACY
#error LIBCDVD_LEGACY not defined, but legacy readCD() included
#endif
int readCD(const char *path, FILEINFO *info, int max)
{
	/* max entries besides "." and "..", for the largest max (getDirComplete) */
	static struct TocEntry TocEntryList[MAX_ENTRY + 1];
	char dir[MAX_PATH];
	int i, j, n, limit;
	u64 wait_start;

	if (sceCdGetDiskType() <= SCECdUNKNOWN) {
		wait_start = Timer();
		while ((Timer() < wait_start + 500) && !uLE_cdDiscValid()) {
			if (cdmode == SCECdNODISC)
				return 0;
		}
		if (cdmode == SCECdNODISC)
			return 0;
		if ((cdmode < SCECdPSCD) || (cdmode > SCECdPS2DVD)) {
			uLE_cdStop();
			return 0;
		}
	}

	strcpy(dir, &path[5]);
	CDVD_FlushCache();
	limit = (max < MAX_ENTRY - 1) ? max + 2 : MAX_ENTRY + 1;
	n = CDVD_GetDir(dir, NULL, CDVD_GET_FILES_AND_DIRS, TocEntryList, limit, dir);
	dirOpenResult(n < 0 ? -EIO : 0);

	for (i = j = 0; i < n && j < max; i++) {
		if (TocEntryList[i].fileProperties & 0x02 &&
		    (!strcmp(TocEntryList[i].filename, ".") ||
		     !strcmp(TocEntryList[i].filename, "..")))
			continue;  //Skip pseudopaths "." and ".."
		strcpy(info[j].name, TocEntryList[i].filename);
		clearMcTable(&info[j].stats);
		if (TocEntryList[i].fileProperties & 0x02) {
			info[j].stats.AttrFile = MC_ATTR_norm_folder;
		} else {
			info[j].stats.AttrFile = MC_ATTR_norm_file;
			info[j].stats.FileSizeByte = TocEntryList[i].fileSize;
			info[j].stats.Reserve2 = 0;  //Assuming a CD can't have a single 4GB file
		}
		j++;
	}

	size_valid = 1;

	return j;
}