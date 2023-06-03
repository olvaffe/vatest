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
#include <sys/mman.h>
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

    VAImageFormat *img_formats;
    unsigned int img_count;

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
va_init_images(struct va *va)
{
    const int format_max = vaMaxNumImageFormats(va->display);

    va->img_formats = malloc(sizeof(*va->img_formats) * format_max);
    if (!va->img_formats)
        va_die("failed to alloc img formats");

    va->status = vaQueryImageFormats(va->display, va->img_formats, &va->img_count);
    va_check(va, "failed to query img formats");
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
    va_init_images(va);
    va_init_subpics(va);
}

static inline void
va_cleanup(struct va *va)
{
    free(va->subpic_formats);
    free(va->img_formats);
    free(va->pairs);
    free(va->attrs);

    vaTerminate(va->display);
    close(va->native_display);
}

static inline const struct va_pair *
va_find_pair(const struct va *va, VAProfile profile, VAEntrypoint entrypoint)
{
    for (int i = 0; i < va->pair_count; i++) {
        const struct va_pair *pair = &va->pairs[i];
        if (pair->profile == profile && pair->entrypoint == entrypoint)
            return pair;
    }
    return NULL;
}

static inline VAConfigID
va_create_config(struct va *va,
                 VAProfile profile,
                 VAEntrypoint entrypoint,
                 unsigned int rt_formats)
{
    VAConfigAttrib attrs[VAConfigAttribTypeMax];
    int attr_count = 0;

    attrs[attr_count].type = VAConfigAttribRTFormat;
    attrs[attr_count++].value = rt_formats;

    VAConfigID config;
    va->status = vaCreateConfig(va->display, profile, entrypoint, attrs, attr_count, &config);
    va_check(va, "failed to create config");

    return config;
}

static inline void
va_destroy_config(struct va *va, VAConfigID config)
{
    va->status = vaDestroyConfig(va->display, config);
    va_check(va, "failed to destroy config");
}

static inline VASurfaceID
va_create_surface(
    struct va *va, unsigned int rt_format, unsigned int width, unsigned int height, int fourcc)
{
    VASurfaceAttrib attrs[VASurfaceAttribCount];
    int attr_count = 0;

    attrs[attr_count].type = VASurfaceAttribPixelFormat;
    attrs[attr_count].value.type = VAGenericValueTypeInteger;
    attrs[attr_count++].value.value.i = fourcc;

    VASurfaceID surf;
    va->status =
        vaCreateSurfaces(va->display, rt_format, width, height, &surf, 1, attrs, attr_count);
    va_check(va, "failed to create surface");

    return surf;
}

static inline void
va_destroy_surface(struct va *va, VASurfaceID surf)
{
    va->status = vaDestroySurfaces(va->display, &surf, 1);
    va_check(va, "failed to destroy surface");
}

static inline void
va_sync_surface(struct va *va, VASurfaceID surf)
{
    va->status = vaSyncSurface(va->display, surf);
    va_check(va, "failed to sync surface");
}

static inline VAContextID
va_create_context(
    struct va *va, VAConfigID config, int width, int height, int flag, VASurfaceID surf)
{
    VAContextID ctx;
    va->status = vaCreateContext(va->display, config, width, height, flag, &surf, 1, &ctx);
    va_check(va, "failed to create context");

    return ctx;
}

static inline void
va_destroy_context(struct va *va, VAContextID ctx)
{
    va->status = vaDestroyContext(va->display, ctx);
    va_check(va, "failed to destroy context");
}

static inline VABufferID
va_create_buffer(
    struct va *va, VAContextID ctx, VABufferType type, unsigned int size, const void *data)
{
    VABufferID buf;
    va->status = vaCreateBuffer(va->display, ctx, type, size, 1, (void *)data, &buf);
    va_check(va, "failed to create buffer");

    return buf;
}

static inline void
va_destroy_buffer(struct va *va, VABufferID buf)
{
    va->status = vaDestroyBuffer(va->display, buf);
    va_check(va, "failed to destroy buffer");
}

static inline void *
va_map_buffer(struct va *va, VABufferID buf)
{
    void *ptr;
    va->status = vaMapBuffer(va->display, buf, &ptr);
    va_check(va, "failed to map buffer");
    return ptr;
}

static inline void
va_unmap_buffer(struct va *va, VABufferID buf)
{
    va->status = vaUnmapBuffer(va->display, buf);
    va_check(va, "failed to unmap buffer");
}

static inline void
va_begin_picture(struct va *va, VAContextID ctx, VASurfaceID surf)
{
    va->status = vaBeginPicture(va->display, ctx, surf);
    va_check(va, "failed to begin picture");
}

static inline void
va_render_picture(struct va *va, VAContextID ctx, const VABufferID *bufs, int count)
{
    va->status = vaRenderPicture(va->display, ctx, (VABufferID *)bufs, count);
    va_check(va, "failed to render picture");
}

static inline void
va_end_picture(struct va *va, VAContextID ctx)
{
    va->status = vaEndPicture(va->display, ctx);
    va_check(va, "failed to end picture");
}

static inline void
va_create_image(struct va *va, int width, int height, int fourcc, VAImage *img)
{
    VAImageFormat format = {
        .fourcc = fourcc,
    };

    va->status = vaCreateImage(va->display, &format, width, height, img);
    va_check(va, "failed to create image");
}

static inline void
va_destroy_image(struct va *va, VAImageID img)
{
    va->status = vaDestroyImage(va->display, img);
    va_check(va, "failed to destroy image");
}

static inline void
va_get_image(
    struct va *va, VASurfaceID surf, unsigned int width, unsigned int height, VAImageID img)
{
    va->status = vaGetImage(va->display, surf, 0, 0, width, height, img);
    va_check(va, "failed to get image");
}

static inline void
va_save_image(struct va *va, const VAImage *img, const char *filename)
{
    if (img->format.fourcc != VA_FOURCC_NV12)
        va_die("only VA_FOURCC_NV12 is supported");

    void *ptr = va_map_buffer(va, img->buf);

    FILE *fp = fopen(filename, "w");
    if (!fp)
        va_die("failed to open %s", filename);

    fprintf(fp, "P6 %u %u %u\n", img->width, img->height, 255);
    for (uint32_t y = 0; y < img->height; y++) {
        for (uint32_t x = 0; x < img->width; x++) {
            const uint8_t *yy = ptr + img->offsets[0] + img->pitches[0] * y + x;
            const uint8_t *uv = ptr + img->offsets[1] + img->pitches[1] * (y / 2) + (x & ~1);

            const int yuv[3] = { (int)*yy, (int)uv[0] - 128, (int)uv[1] - 128 };
            const int rgb[3] = { yuv[0] + 1.402000f * yuv[2],
                                 yuv[0] - 0.344136f * yuv[1] - 0.714136f * yuv[2],
                                 yuv[0] + 1.772000f * yuv[1] };
#define CLAMP(v) v > 255 ? 255 : v < 0 ? 0 : v
            const char bytes[3] = { CLAMP(rgb[0]), CLAMP(rgb[1]), CLAMP(rgb[2]) };
#undef CLAMP
            if (fwrite(bytes, sizeof(bytes), 1, fp) != 1)
                va_die("failed to write pixel (%u, %u)", x, y);
        }
    }

    fclose(fp);

    va_unmap_buffer(va, img->buf);
}

static inline const void *
va_map_file(struct va *va, const char *filename, size_t *out_size)
{
    const int fd = open(filename, O_RDONLY);
    if (fd < 0)
        va_die("failed to open %s", filename);

    const off_t size = lseek(fd, 0, SEEK_END);
    if (size < 0)
        va_die("failed to seek file");

    const void *ptr = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED)
        va_die("failed to map file");

    close(fd);

    *out_size = size;
    return ptr;
}

static inline void
va_unmap_file(struct va *va, const void *ptr, size_t size)
{
    munmap((void *)ptr, size);
}

#endif /* VAUTIL_H */
