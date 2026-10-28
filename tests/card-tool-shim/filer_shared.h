#ifndef FILER_SHARED_H
#define FILER_SHARED_H
#define TRANSFER_STACK_READY 0
extern int fileMode;
extern int vmcMounted[2];
int prepareTransferDeviceStacks(const char *src_path, const char *dst_path);
#endif
