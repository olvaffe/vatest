/*
 * Copyright 2022 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "vautil.h"

static void
info_pair_attr(const struct va *va, const struct va_pair *pair, const VAConfigAttrib *attr)
{
    if (attr->value == VA_ATTRIB_NOT_SUPPORTED)
        return;

    char str[1024];
    int format;
    switch (attr->type) {
    case VAConfigAttribRTFormat:
        format = 'x';
        break;
    default:
        format = 'd';
        break;
    }

    const char *name = vaConfigAttribTypeStr(attr->type) + 14;
    switch (format) {
    case 'x':
        va_log("  %s: 0x%x", name, attr->value);
        break;
    case 's':
        va_log("  %s: %s", name, str);
        break;
    default:
        va_log("  %s: %d", name, attr->value);
        break;
    }
}

static void
info_pairs(const struct va *va)
{
    for (int i = 0; i < va->pair_count; i++) {
        const struct va_pair *pair = &va->pairs[i];

        va_log("pair: (%s, %s)", vaProfileStr(pair->profile), vaEntrypointStr(pair->entrypoint));
        for (int j = 0; j < VAConfigAttribTypeMax; j++) {
            const VAConfigAttrib *attr = &pair->attrs[j];
            info_pair_attr(va, pair, attr);
        }
    }
}

static void
info_display(const struct va *va)
{
    va_log("version: %d.%d", va->major, va->minor);
    va_log("vendor: %s", va->vendor);
}

int
main(void)
{

    struct va va;
    va_init(&va, NULL);

    info_display(&va);
    info_pairs(&va);

    va_cleanup(&va);

    return 0;
}
