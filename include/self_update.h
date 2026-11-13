#ifndef WLE_SELF_UPDATE_H
#define WLE_SELF_UPDATE_H

#ifdef DS34
#define SELF_UPDATE_FOLDER "APP_LAUNCHSTATION-DS"
#define SELF_UPDATE_ELF "LAUNCHSTATION-DS.ELF"
#else
#define SELF_UPDATE_FOLDER "APP_LAUNCHSTATION"
#define SELF_UPDATE_ELF "LAUNCHSTATION.ELF"
#endif
#define SELF_UPDATE_ASSET SELF_UPDATE_FOLDER ".psu"

int selfUpdateExtractPsu(const char *package, const char *stage, int size);
int selfUpdateExtractPsuSidecar(const char *package, const char *name,
                                const char *stage, int size);
/* The same checks on a package already held in RAM. */
int selfUpdateExtractPsuMem(const unsigned char *package, int size, const char *stage);
int selfUpdateExtractPsuSidecarMem(const unsigned char *package, int size,
                                   const char *name, const char *stage);

int selfUpdateLocatePsuElf(const unsigned char *package, int size, int *offset);

/* Called by the PSU extractor after each block of the app ELF it writes. */
extern void (*selfUpdateWriteProgress)(int written, int total);

/* package_bytes: the release asset's size; the package is downloaded into RAM, so 0 (unknown) is refused. */
int selfUpdateInstall(const char *ca_path, const char *expected_sha256, int package_bytes,
                      char *result, int result_size);

#endif
