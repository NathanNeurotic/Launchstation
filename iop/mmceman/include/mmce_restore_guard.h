#ifndef MMCE_RESTORE_GUARD_H
#define MMCE_RESTORE_GUARD_H

/* Firmware transports MMCE paths with a single u8 byte count including
 * NUL. Validate before narrowing size_t to u8, especially before write,
 * rename and remove commands. These helpers perform no device I/O. */
#include <stddef.h>
#include <string.h>

#define MMCE_FS_ACTIVE_NAME "__active_card__.mcd"
#define MMCE_FS_WIRE_NAME_MAX 254

static int mmce_fs_wire_name_ok(const char *name)
{
    size_t i;

    if (name == NULL || name[0] == '\0')
        return 0;
    for (i = 0; i < MMCE_FS_WIRE_NAME_MAX && name[i]; ++i)
        ;
    return name[i] == '\0';
}

/* The active-card name is a synthetic, read-only descriptor, not a physical
 * file to create, truncate, rename or remove. Reserve this entire component
 * even when callers prepend folders, ./, or a separator alias. */
static int mmce_fs_reserved_component(const char *name)
{
    const char *p = name;
    static const char reserved[] = MMCE_FS_ACTIVE_NAME;

    if (!name)
        return 0;
    while (*p) {
        const char *start;
        while (*p == '/' || *p == '\\')
            ++p;
        if (!*p)
            break;
        start = p;
        while (*p && *p != '/' && *p != '\\')
            ++p;
        if ((size_t)(p - start) == sizeof(reserved) - 1 &&
            !memcmp(start, reserved, sizeof(reserved) - 1))
            return 1;
    }
    return 0;
}

#endif /* MMCE_RESTORE_GUARD_H */
