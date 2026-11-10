#ifndef FILER_SHARED_H
#define FILER_SHARED_H
#define TRANSFER_STACK_READY 0
extern int fileMode;
extern int vmcMounted[2];
int prepareTransferDeviceStacks(const char *src_path, const char *dst_path);
/* mc0:/mc1: for a save written file by file: free 1 KB clusters now (<0:
 * no formatted PS2 card), and a card entry's mode, times and attribute */
int filerMcFreeClusters(int port);
int filerMcSetEntry(const char *path, const unsigned char *entry);
#endif
