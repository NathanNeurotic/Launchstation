#ifndef WLE_IGR_CONFIG_H
#define WLE_IGR_CONFIG_H

#define WLE_IGR_MAGIC "WLEIGR_CFG_V1"
#define WLE_IGR_PATH 256

typedef struct {
    char magic[16];
    char app[WLE_IGR_PATH];
    char backup[WLE_IGR_PATH];
} WleIgrEmbeddedConfig;

#endif
