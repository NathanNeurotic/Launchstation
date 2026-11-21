#include "hdd_install.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const paths[HDD_INSTALL_ASSETS] = {
    "scripts/assets/osdmenu/OSDMBR.XLF", "scripts/assets/osdmenu/hosdmenu.elf",
    "games/APPS/APP_WLE-R3Z.psu", "games/APPS/SYS_R3CONFIGURATOR.psu",
    NULL, "scripts/assets/osdmenu/version.txt", NULL, NULL, "icons/art/APP_WLER3Z.png", "icons/art/SYS_R3CONFIG.png"
};
static const char *const names[HDD_INSTALL_ASSETS] = {
    "OSDMBR.XLF", "hosdmenu.elf", "APP_WLE-R3Z.psu", "SYS_R3CONFIGURATOR.psu", "extras.zip", "version.txt", "system.tar.gz", "language.tar.gz", "APP_WLER3Z.png", "SYS_R3CONFIG.png"
};
const char *const hddInstallOsdFiles[7] = {
    "OSDSYS_A.XLF", "FNTOSD", "ICOIMAGE", "JISUCS", "SKBIMAGE", "SNDIMAGE", "TEXIMAGE"
};
const char *hddInstallAssetPath(unsigned asset) { return asset < HDD_INSTALL_ASSETS ? paths[asset] : NULL; }
const char *hddInstallAssetName(unsigned asset) { return asset < HDD_INSTALL_ASSETS ? names[asset] : NULL; }
int hddInstallNeedsAsset(unsigned flags, unsigned asset)
{
    if (asset >= HDD_INSTALL_ASSETS) return 0;
    if (asset == HI_EXTRAS && (flags & HDD_INSTALL_PSBBN)) return 0;
    if (asset == HI_WLE_ART || asset == HI_R3_ART) return (flags & (HDD_INSTALL_PSBBN | HDD_INSTALL_APPS)) == (HDD_INSTALL_PSBBN | HDD_INSTALL_APPS);
    if (asset == HI_SYSTEM || asset == HI_LANGUAGE) return !!(flags & HDD_INSTALL_PSBBN);
    return (asset == HI_WLE || asset == HI_R3CONFIG) ? !!(flags & HDD_INSTALL_APPS) : !!(flags & HDD_INSTALL_HOSD);
}
int hddInstallHex(const char *text, unsigned length)
{
    unsigned i;
    if (!text || strlen(text) != length) return 0;
    for (i = 0; i < length; i++) if (!isxdigit((unsigned char)text[i])) return 0;
    return 1;
}

/* These parsers accept complete JSON values only. In particular, an asset's
 * hash/size cannot be taken from an adjacent object or from a nested field. */
static const char *space(const char *p) { while (*p && isspace((unsigned char)*p)) p++; return p; }
static const char *string(const char *p, char *out, size_t capacity)
{
    size_t n = 0;
    if (*p++ != '"') return NULL;
    while (*p && *p != '"') {
        unsigned char ch = (unsigned char)*p++;
        if (ch < 32) return NULL;
        if (ch == '\\') {
            ch = (unsigned char)*p++;
            if (!ch) return NULL;
            if (ch == 'u') {
                unsigned i;
                for (i = 0; i < 4; i++) if (!*p || !isxdigit((unsigned char)*p++)) return NULL;
                ch = '?';
            } else if (ch != '"' && ch != '\\' && ch != '/' && !strchr("bfnrt", ch)) return NULL;
        }
        if (out) {
            if (n + 1 >= capacity) return NULL;
            out[n] = (char)ch;
        }
        n++;
    }
    if (*p != '"') return NULL;
    if (out) out[n] = 0;
    return p + 1;
}
static const char *skip(const char *p, unsigned depth)
{
    const char *q;
    char close;
    if (depth > 16) return NULL;
    p = space(p);
    if (*p == '"') return string(p, NULL, 0);
    if (*p == '{' || *p == '[') {
        int object = *p == '{';
        close = object ? '}' : ']'; p = space(p + 1);
        if (*p == close) return p + 1;
        for (;;) {
            if (object) {
                p = string(p, NULL, 0);
                if (!p || *(p = space(p)) != ':') return NULL;
                p++;
            }
            p = skip(p, depth + 1);
            if (!p) return NULL;
            p = space(p);
            if (*p == close) return p + 1;
            if (*p != ',') return NULL;
            p = space(p + 1);
        }
    }
    for (q = p; *q && !strchr(",}] \t\r\n", *q); q++);
    if (q == p) return NULL;
    if ((q - p == 4 && (!memcmp(p, "null", 4) || !memcmp(p, "true", 4))) ||
        (q - p == 5 && !memcmp(p, "false", 5))) return q;
    /* Only JSON's number grammar may appear as an unquoted value. */
    if (*p == '-') p++;
    if (*p == '0') p++;
    else {
        if (*p < '1' || *p > '9') return NULL;
        while (isdigit((unsigned char)*p)) p++;
    }
    if (*p == '.') { p++; if (!isdigit((unsigned char)*p)) return NULL; while (isdigit((unsigned char)*p)) p++; }
    if (*p == 'e' || *p == 'E') {
        p++; if (*p == '+' || *p == '-') p++;
        if (!isdigit((unsigned char)*p)) return NULL;
        while (isdigit((unsigned char)*p)) p++;
    }
    return p == q ? q : NULL;
}

static const char *field(const char *p, const char *wanted)
{
    char key[128];
    const char *value = NULL;
    if (!p || *(p = space(p)) != '{') return NULL;
    p = space(p + 1);
    if (*p == '}') return NULL;
    for (;;) {
        p = string(p, key, sizeof(key));
        if (!p || *(p = space(p)) != ':') return NULL;
        p = space(p + 1);
        if (!strcmp(key, wanted)) { if (value) return NULL; value = p; }
        p = skip(p, 0);
        if (!p) return NULL;
        p = space(p);
        if (*p == '}') return value;
        if (*p != ',') return NULL;
        p = space(p + 1);
    }
}
int hddInstallCommit(const char *json, char commit[41])
{
    const char *end, *object, *hash;
    char type[16];
    if (!json || !(end = skip(json, 0)) || *space(end)) return 0;
    object = field(json, "object");
    hash = field(object, "sha");
    end = field(object, "type");
    return hash && end && string(end, type, sizeof(type)) && !strcmp(type, "commit") &&
        string(hash, commit, 41) && hddInstallHex(commit, 40);
}
int hddInstallAssetMetadata(const char *json, const char *name, char hash[41], uint32_t *size)
{
    const char *p, *end;
    char found[128], type[16];
    int matches = 0;
    if (!json || !name || !hash || !size || !(end = skip(json, 0)) || *space(end)) return 0;
    p = space(json);
    if (*p++ != '[') return 0;
    p = space(p);
    while (*p != ']') {
        const char *n = field(p, "name"), *h, *v, *t;
        if (!n || !string(n, found, sizeof(found))) return 0;
        if (!strcmp(found, name)) {
            unsigned long count;
            char *stop;
            h = field(p, "sha"); v = field(p, "size"); t = field(p, "type");
            if (++matches > 1 || !h || !v || !t || !string(t, type, sizeof(type)) || strcmp(type, "file") ||
                !string(h, hash, 41) || !hddInstallHex(hash, 40) || !isdigit((unsigned char)*v)) return 0;
            count = strtoul(v, &stop, 10);
            if (!count || count > 16u * 1024 * 1024 || (*stop != ',' && *stop != '}' && !isspace((unsigned char)*stop))) return 0;
            *size = (uint32_t)count;
        }
        p = skip(p, 0);
        if (!p) return 0;
        p = space(p);
        if (*p == ']') break;
        if (*p != ',') return 0;
        p = space(p + 1);
    }
    return matches == 1;
}

int hddInstallConfig(const HddInstall *in, int mbr, char *out, size_t capacity)
{
    const char *apps;
    if (!in || (in->flags & ~(HDD_INSTALL_HOSD | HDD_INSTALL_APPS | HDD_INSTALL_PSBBN))) return -EINVAL;
    if ((in->flags & HDD_INSTALL_PSBBN) && !in->jail) return -EINVAL;
    apps = in->jail ? "ata0:/APPS/" : "hdd0:__common:pfs:/APPS/";
    int used, n;
    if (!out || !capacity || !(in->flags & HDD_INSTALL_HOSD)) return -EINVAL;
    if (mbr) {
        used = snprintf(out, capacity,
            "# Installed by Launchstation from PSBBN DP %.7s\n"
            "boot_auto = %s\n%sboot_cross = $HOSDSYS\n"
            "cdrom_skip_ps2logo = 0\ncdrom_disable_gameid = 0\ncdrom_use_dkwdrv = 0\n"
            "ps1drv_enable_fast = 0\nps1drv_enable_smooth = 0\nps1drv_use_ps1vn = 1\n"
            "app_gameid = 1\nprefer_bbn = %u\n%s", in->commit,
            in->flags & HDD_INSTALL_PSBBN ? "$PSBBN" : "$HOSDSYS",
            in->flags & HDD_INSTALL_PSBBN ? "boot_auto_arg1 = -dev9=NICHDD\n" : "",
            !!(in->flags & HDD_INSTALL_PSBBN), in->flags & HDD_INSTALL_PSBBN ? "osd_language = eng\n" : "");
        if (used < 0 || (size_t)used >= capacity) return -ENOSPC;
        if (in->flags & HDD_INSTALL_APPS) {
            n = snprintf(out + used, capacity - used,
                "boot_start = %sAPP_WLE-R3Z/WLE-R3Z.ELF\n"
                "boot_select = %sSYS_R3CONFIGURATOR/r3configurator.elf\n", apps, apps);
            if (n < 0 || (size_t)n >= capacity - used) return -ENOSPC;
            used += n;
        }
    } else {
        used = snprintf(out, capacity,
            "# Installed by Launchstation from PSBBN DP %.7s\n"
            "boot_auto = $HOSDSYS\nOSDSYS_video_mode = AUTO\n"
            "OSDSYS_selected_color = 0x10,0x80,0xE0,0x80\n"
            "OSDSYS_unselected_color = 0x33,0x33,0x33,0x80\n"
            "OSDSYS_scroll_menu = 1\nOSDSYS_menu_x = 320\nOSDSYS_menu_y = 110\n"
            "OSDSYS_enter_x = 30\nOSDSYS_enter_y = -1\nOSDSYS_version_x = -1\nOSDSYS_version_y = -1\n"
            "OSDSYS_cursor_max_velocity = 1500\nOSDSYS_cursor_acceleration = 150\n"
            "OSDSYS_left_cursor =\nOSDSYS_right_cursor =\nOSDSYS_menu_top_delimiter =\n"
            "OSDSYS_menu_bottom_delimiter =\nOSDSYS_num_displayed_items = 5\nOSDSYS_Skip_Disc = 0\n"
            "cdrom_skip_ps2logo = 0\ncdrom_disable_gameid = 0\ncdrom_use_dkwdrv = 0\n"
            "ps1drv_enable_fast = 0\nps1drv_enable_smooth = 0\nps1drv_use_ps1vn = 1\napp_gameid = 1\n", in->commit);
        if (used < 0 || (size_t)used >= capacity) return -ENOSPC;
        if (in->flags & HDD_INSTALL_APPS) {
            n = snprintf(out + used, capacity - used,
                "name_OSDSYS_ITEM_1 = wLaunchELF-R3Z\npath1_OSDSYS_ITEM_1 = %sAPP_WLE-R3Z/WLE-R3Z.ELF\n"
                "name_OSDSYS_ITEM_2 = R3CONFIGURATOR\npath1_OSDSYS_ITEM_2 = %sSYS_R3CONFIGURATOR/r3configurator.elf\n",
                apps, apps);
            if (n < 0 || (size_t)n >= capacity - used) return -ENOSPC;
            used += n;
        }
    }
    return used;
}

/* Archive.org metadata has a files array; choose the newest complete semantic
 * version, ignoring torrents/metadata and rejecting ambiguous duplicate names. */
int hddInstallArchiveMetadata(const char *json, const char *prefix,
                              char name[128], char hash[41], uint32_t *size)
{
    const char *p, *end;
    unsigned best[3] = {0}, matches = 0;
    if (!json || !prefix || !name || !hash || !size || !(end = skip(json, 0)) || *space(end)) return 0;
    p = field(json, "files");
    if (!p || *p++ != '[') return 0;
    p = space(p);
    while (*p != ']') {
        char candidate[256];
        const char *n = field(p, "name");
        if (!n || !string(n, candidate, sizeof(candidate))) return 0;
        if (!strncmp(candidate, prefix, strlen(prefix))) {
            unsigned ver[3], k;
            const char *q = candidate + strlen(prefix);
            for (k = 0; k < 3; k++) {
                char *stop; unsigned long v;
                if (!isdigit((unsigned char)*q)) break;
                v = strtoul(q, &stop, 10); if (v > 65535) break;
                ver[k] = v; q = stop;
                if (k < 2 && *q++ != '.') break;
            }
            if (k == 3 && !strcmp(q, ".tar.gz")) {
                int newer = !matches, equal = 1;
                for (k = 0; k < 3; k++) if (ver[k] != best[k]) { newer = ver[k] > best[k]; equal = 0; break; }
                if (matches && equal) return 0;
                if (newer) {
                    const char *h = field(p, "sha1"), *v = field(p, "size");
                    char count[24], *stop; unsigned long bytes;
                    if (!h || !v || !string(h, hash, 41) || !hddInstallHex(hash, 40) ||
                        !string(v, count, sizeof(count)) || !isdigit((unsigned char)count[0])) return 0;
                    bytes = strtoul(count, &stop, 10);
                    if (*stop || !bytes || bytes > 256u * 1024 * 1024 || strlen(candidate) >= 128) return 0;
                    memcpy(best, ver, sizeof(best)); strcpy(name, candidate); *size = bytes; matches++;
                }
            }
        }
        p = skip(p, 0); if (!p) return 0;
        p = space(p); if (*p == ']') break;
        if (*p != ',') return 0;
        p = space(p + 1);
    }
    return matches != 0;
}
