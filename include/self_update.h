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

/* package_bytes: the release asset's size, or 0 when unknown (no space pre-check). */
int selfUpdateInstall(const char *ca_path, const char *expected_sha256, int package_bytes,
                      char *result, int result_size);

#endif
