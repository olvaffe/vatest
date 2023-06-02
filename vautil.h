/*
 * Copyright 2022 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef VAUTIL_H
#define VAUTIL_H

#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <va/va.h>
#include <va/va_drm.h>
#include <va/va_str.h>
#include <xf86drm.h>

#define PRINTFLIKE(f, a) __attribute__((format(printf, f, a)))
#define NORETURN __attribute__((noreturn))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct va_init_params {
    int unused;
};

struct va_pair {
    VAProfile profile;
    VAEntrypoint entrypoint;

    VAConfigAttrib attrs[VAConfigAttribTypeMax];
};

struct va {
    struct va_init_params params;

    VAStatus status;

    int native_display;
    VADisplay display;
    int major;
    int minor;
    const char *vendor;
    VADisplayAttribute *attrs;
    int attr_count;

    struct va_pair *pairs;
    int pair_count;

    VAImageFormat *subpic_formats;
    unsigned int *subpic_flags;
    unsigned int subpic_count;
};

static inline void
va_logv(const char *format, va_list ap)
{
    printf("VA: ");
    vprintf(format, ap);
    printf("\n");
}

static inline void NORETURN
va_diev(const char *format, va_list ap)
{
    va_logv(format, ap);
    abort();
}

static inline void PRINTFLIKE(1, 2) va_log(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    va_logv(format, ap);
    va_end(ap);
}

static inline void PRINTFLIKE(1, 2) NORETURN va_die(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    va_diev(format, ap);
    va_end(ap);
}

static inline void PRINTFLIKE(2, 3) va_check(const struct va *va, const char *format, ...)
{
    if (va->status == VA_STATUS_SUCCESS)
        return;

    va_list ap;
    va_start(ap, format);
    va_diev(format, ap);
    va_end(ap);
}

static inline void
va_init_display_drm(struct va *va)
{
    drmDevicePtr devs[64];
    int dev_count = drmGetDevices2(0, devs, ARRAY_SIZE(devs));

    int fd = -1;
    for (int i = 0; i < dev_count; i++) {
        const int type = DRM_NODE_RENDER;
        drmDevicePtr dev = devs[i];

        if (!(dev->available_nodes & (1 << type)))
            continue;

        fd = open(dev->nodes[type], O_RDWR | O_CLOEXEC);
        if (fd >= 0)
            break;
    }

    drmFreeDevices(devs, dev_count);

    if (fd < 0)
        va_die("failed to find any render node");

    va->native_display = fd;
}

static inline void
va_init_display(struct va *va)
{
    va_init_display_drm(va);
    va->display = vaGetDisplayDRM(va->native_display);
    if (!va->display)
        va_die("failed to get display");

    va->status = vaInitialize(va->display, &va->major, &va->minor);
    va_check(va, "failed to initialize display");

    va->vendor = vaQueryVendorString(va->display);

    const int attr_max = vaMaxNumDisplayAttributes(va->display);
    va->attrs = malloc(sizeof(*va->attrs) * attr_max);
    if (!va->attrs)
        va_die("failed to alloc display attrs");
    va->status = vaQueryDisplayAttributes(va->display, va->attrs, &va->attr_count);
    va_check(va, "failed to query display attrs");

    for (int i = 0; i < va->attr_count; i++) {
        VADisplayAttribute *attr = &va->attrs[i];
        if (!(attr->flags & VA_DISPLAY_ATTRIB_GETTABLE))
            continue;

        va->status = vaGetDisplayAttributes(va->display, attr, 1);
        va_check(va, "failed to get display attr value");
    }
}

static inline void
va_init_pairs(struct va *va)
{
    const int profile_max = vaMaxNumProfiles(va->display);
    VAProfile *profiles = malloc(sizeof(*profiles) * profile_max);
    if (!profiles)
        va_die("failed to alloc profiles");

    const int entrypoint_max = vaMaxNumEntrypoints(va->display);
    VAEntrypoint *entrypoints = malloc(sizeof(*entrypoints) * entrypoint_max);
    if (!entrypoints)
        va_die("failed to alloc entrypoints");

    int profile_count;
    va->status = vaQueryConfigProfiles(va->display, profiles, &profile_count);
    va_check(va, "failed to query profiles");

    for (int i = 0; i < profile_count; i++) {
        int entrypoint_count;
        va->status =
            vaQueryConfigEntrypoints(va->display, profiles[i], entrypoints, &entrypoint_count);
        va_check(va, "failed to query entrypoints");
        va->pair_count += entrypoint_count;
    }

    va->pairs = malloc(sizeof(*va->pairs) * va->pair_count);
    if (!va->pairs)
        va_die("failed to alloc pairs");

    struct va_pair *pair = va->pairs;
    for (int i = 0; i < profile_count; i++) {
        int entrypoint_count;
        va->status =
            vaQueryConfigEntrypoints(va->display, profiles[i], entrypoints, &entrypoint_count);
        va_check(va, "failed to query entrypoints");

        for (int j = 0; j < entrypoint_count; j++) {
            assert(pair < va->pairs + va->pair_count);

            pair->profile = profiles[i];
            pair->entrypoint = entrypoints[j];
            for (int k = 0; k < VAConfigAttribTypeMax; k++)
                pair->attrs[k].type = k;

            va->status = vaGetConfigAttributes(va->display, pair->profile, pair->entrypoint,
                                               pair->attrs, VAConfigAttribTypeMax);
            va_check(va, "failed to get config attrs");

            pair++;
        }
    }

    free(profiles);
    free(entrypoints);
}

static inline void
va_init_subpics(struct va *va)
{
    const int format_max = vaMaxNumSubpictureFormats(va->display);

    va->subpic_formats =
        malloc((sizeof(*va->subpic_formats) + sizeof(*va->subpic_flags)) * format_max);
    if (!va->subpic_formats)
        va_die("failed to alloc subpic formats");
    va->subpic_flags = (void *)(va->subpic_formats + format_max);

    va->status = vaQuerySubpictureFormats(va->display, va->subpic_formats, va->subpic_flags,
                                          &va->subpic_count);
    va_check(va, "failed to query subpic formats");
}

static inline void
va_init(struct va *va, const struct va_init_params *params)
{
    memset(va, 0, sizeof(*va));
    if (params)
        va->params = *params;

    va_init_display(va);
    va_init_pairs(va);
    va_init_subpics(va);
}

static inline void
va_cleanup(struct va *va)
{
    free(va->subpic_formats);
    free(va->pairs);
    free(va->attrs);

    vaTerminate(va->display);
    close(va->native_display);
}

#endif /* VAUTIL_H */
