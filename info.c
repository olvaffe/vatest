/*
 * Copyright 2022 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "vautil.h"

static void
info_subpics(const struct va *va)
{
    va_log("subpicture formats:");
    for (unsigned int i = 0; i < va->subpic_count; i++) {
        const VAImageFormat *fmt = &va->subpic_formats[i];
        const char *fourcc = (const char *)&fmt->fourcc;
        unsigned flags = va->subpic_flags[i];

        va_log("  %c%c%c%c: 0x%x", fourcc[0], fourcc[1], fourcc[2], fourcc[3], flags);
    }
}

static void
info_images(const struct va *va)
{
    va_log("image formats:");
    for (unsigned int i = 0; i < va->img_count; i++) {
        const VAImageFormat *fmt = &va->img_formats[i];
        const char *fourcc = (const char *)&fmt->fourcc;

        va_log("  %c%c%c%c", fourcc[0], fourcc[1], fourcc[2], fourcc[3]);
    }
}

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
    va_log("attrs:");

    for (int i = 0; i < va->attr_count; i++) {
        const VADisplayAttribute *attr = &va->attrs[i];
        if (attr->flags == VA_DISPLAY_ATTRIB_NOT_SUPPORTED)
            continue;

        switch (attr->type) {
        case VADisplayAttribCopy:
            va_log("  Copy: 0x%x", attr->value);
            break;
        case VADisplayPCIID:
            va_log("  PCIID: 0x%04x:0x%04x", (attr->value >> 16) & 0xffff, attr->value & 0xffff);
            break;
        default:
            va_log("  type %d: min %d max %d val %d flags 0x%x", attr->type, attr->min_value,
                   attr->max_value, attr->value, attr->flags);
            break;
        }
    }
}

int
main(void)
{

    struct va va;
    va_init(&va, NULL);

    info_display(&va);
    info_pairs(&va);
    info_images(&va);
    info_subpics(&va);

    va_cleanup(&va);

    return 0;
}
