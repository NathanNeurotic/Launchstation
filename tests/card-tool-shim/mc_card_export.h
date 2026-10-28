#ifndef MC_CARD_EXPORT_H
#define MC_CARD_EXPORT_H
enum { MC_CARD_EXPORT_OK = 0, MC_CARD_EXPORT_IO = -3 };
enum { MC_CARD_EXPORT_WRITE = 0, MC_CARD_EXPORT_VERIFY = 1 };
typedef void (*McCardExportProgress)(int phase, u64 done, u64 total);
int mcCardBackupNoEccNewFile(const char *source, const char *dest, McCardExportProgress progress);
#endif
