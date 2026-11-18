#ifndef GUI_SORT_H
#define GUI_SORT_H

#include "launchelf.h"

/* Shared APA priority for HDD Manager and all HDD file lists. */
int hddPartitionPriority(const char *name);

int cmpFile(FILEINFO *a, FILEINFO *b);
void sort(FILEINFO *a, int left, int right);

#endif
