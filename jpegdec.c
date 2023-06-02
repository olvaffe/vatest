/*
 * Copyright 2022 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "vautil.h"

struct jpegdec_test_file {
    const void *ptr;
    size_t size;

    const void *soi;

    struct {
        const void *segments[4];
        int Pq[4];
        int Tq[4];
        const void *Qk[4];
    } dqt;

    struct {
        const void *segment;
        int P;
        int Y;
        int X;
        int Nf;
        int Ci[4];
        int Hi[4];
        int Vi[4];
        int Tqi[4];
    } sof0;

    struct {
        const void *segments[4];
        int Tc[4];
        int Th[4];
        const void *Li[4];
        const void *Vij[4];
        int Vij_sizes[4];
    } dht;

    struct {
        const void *segment;
        int Ns;
        int Csj[4];
        int Tdj[4];
        int Taj[4];
    } sos;

    const void *scan;
    int scan_size;

    struct {
        const void *segment;
        int Ri;
    } dri;

    const void *eoi;
};

struct jpegdec_test {
    VAProfile profile;
    VAEntrypoint entrypoint;

    struct va va;

    struct jpegdec_test_file file;

    VASurfaceID surface;
    VAConfigID config;
    VAContextID context;

    VABufferID pic_param;
    VABufferID iq_matrix;
    VABufferID huffman_table;
    VABufferID slice_param;
    VABufferID slice_data;
};

static int
segment_param_be16(const unsigned char *stream)
{
    return (int)stream[0] * 256 + stream[1];
}

static void
jpegdec_test_init(struct jpegdec_test *test)
{
    struct va *va = &test->va;

    va_init(va, NULL);
}

static void
jpegdec_test_dump(struct jpegdec_test *test, const char *filename)
{
    const struct jpegdec_test_file *file = &test->file;
    struct va *va = &test->va;
    VAImage img;

    va_create_image(va, file->sof0.X, file->sof0.Y, VA_FOURCC_BGRA, &img);
    va_get_image(va, test->surface, file->sof0.X, file->sof0.Y, img.image_id);
    va_save_image(va, &img, filename);
    va_destroy_image(va, img.image_id);
}

static void
jpegdec_test_decode(struct jpegdec_test *test)
{
    struct va *va = &test->va;

    const VABufferID bufs[] = {
        test->pic_param,   test->iq_matrix,  test->huffman_table,
        test->slice_param, test->slice_data,
    };
    va_begin_picture(va, test->context, test->surface);
    va_render_picture(va, test->context, bufs, ARRAY_SIZE(bufs));
    va_end_picture(va, test->context);

    va_sync_surface(va, test->surface);
}

static void
jpegdec_test_prepare(struct jpegdec_test *test)
{
    const unsigned int rt_format = VA_RT_FORMAT_YUV420;
    const unsigned int pix_format = VA_FOURCC_NV12;
    const struct jpegdec_test_file *file = &test->file;
    struct va *va = &test->va;

    VAPictureParameterBufferJPEGBaseline pic_param = {
        .picture_width = file->sof0.X,
        .picture_height = file->sof0.Y,
        .num_components = file->sof0.Nf,
    };
    for (int i = 0; i < file->sof0.Nf; i++) {
        pic_param.components[i].component_id = file->sof0.Ci[i];
        pic_param.components[i].h_sampling_factor = file->sof0.Hi[i];
        pic_param.components[i].v_sampling_factor = file->sof0.Vi[i];
        pic_param.components[i].quantiser_table_selector = file->sof0.Tqi[i];
    }

    VAIQMatrixBufferJPEGBaseline iq_matrix = { 0 };
    for (unsigned int i = 0; i < ARRAY_SIZE(file->dqt.Qk); i++) {
        if (!file->dqt.Qk[i])
            break;
        if (file->dqt.Pq[i])
            va_die("no 16-bit Q support");

        const int Tq = file->dqt.Tq[i];
        iq_matrix.load_quantiser_table[Tq] = 1;
        memcpy(iq_matrix.quantiser_table[Tq], file->dqt.Qk[i], 64);
    }

    VAHuffmanTableBufferJPEGBaseline huffman_table = { 0 };
    for (unsigned int i = 0; i < ARRAY_SIZE(file->dht.Li); i++) {
        if (!file->dht.Li[i])
            break;

        const int Tc = file->dht.Tc[i];
        const int Th = file->dht.Th[i];
        huffman_table.load_huffman_table[Th] = 1;
        if (Tc) {
            memcpy(huffman_table.huffman_table[Th].num_ac_codes, file->dht.Li[i], 16);
            memcpy(huffman_table.huffman_table[Th].ac_values, file->dht.Vij[i],
                   file->dht.Vij_sizes[i]);
        } else {
            memcpy(huffman_table.huffman_table[Th].num_dc_codes, file->dht.Li[i], 16);
            memcpy(huffman_table.huffman_table[Th].dc_values, file->dht.Vij[i],
                   file->dht.Vij_sizes[i]);
        }
    }

    VASliceParameterBufferJPEGBaseline slice_param = {
        .slice_data_size = file->scan_size,
        .slice_data_flag = VA_SLICE_DATA_FLAG_ALL,
        .num_components = file->sos.Ns,
    };
    for (int i = 0; i < file->sos.Ns; i++) {
        slice_param.components[i].component_selector = file->sos.Csj[i];
        slice_param.components[i].dc_table_selector = file->sos.Tdj[i];
        slice_param.components[i].ac_table_selector = file->sos.Taj[i];
    }
    slice_param.restart_interval = file->dri.Ri;

    const int mcu_cols = file->sof0.X / (file->sof0.Hi[0] * 8);
    const int mcu_rows = file->sof0.Y / (file->sof0.Vi[0] * 8);
    slice_param.num_mcus = mcu_cols * mcu_rows;

    test->config = va_create_config(va, test->profile, test->entrypoint, rt_format);
    test->surface = va_create_surface(va, rt_format, file->sof0.X, file->sof0.Y, pix_format);
    test->context = va_create_context(va, test->config, file->sof0.X, file->sof0.Y,
                                      VA_PROGRESSIVE, test->surface);

    test->pic_param = va_create_buffer(va, test->context, VAPictureParameterBufferType,
                                       sizeof(pic_param), &pic_param);
    test->iq_matrix =
        va_create_buffer(va, test->context, VAIQMatrixBufferType, sizeof(iq_matrix), &iq_matrix);
    test->huffman_table = va_create_buffer(va, test->context, VAHuffmanTableBufferType,
                                           sizeof(huffman_table), &huffman_table);
    test->slice_param = va_create_buffer(va, test->context, VASliceParameterBufferType,
                                         sizeof(slice_param), &slice_param);

    test->slice_data =
        va_create_buffer(va, test->context, VASliceDataBufferType, file->scan_size, file->scan);
}

static void
jpegdec_test_parse_file_dri(struct jpegdec_test *test)
{
    struct jpegdec_test_file *file = &test->file;
    if (!file->dri.segment)
        return;

    const unsigned char *stream = file->dri.segment + 4;
    file->dri.Ri = segment_param_be16(stream);
}

static void
jpegdec_test_parse_file_sos(struct jpegdec_test *test)
{
    struct jpegdec_test_file *file = &test->file;
    const unsigned char *stream = file->sos.segment + 4;

    file->sos.Ns = stream[0];
    for (int i = 0; i < file->sos.Ns; i++) {
        file->sos.Csj[i] = stream[1 + 2 * i];
        file->sos.Tdj[i] = stream[1 + 2 * i + 1] >> 4;
        file->sos.Taj[i] = stream[1 + 2 * i + 1] & 0xf;
    }
}

static void
jpegdec_test_parse_file_dht(struct jpegdec_test *test)
{
    struct jpegdec_test_file *file = &test->file;
    unsigned int count = 0;

    for (unsigned int i = 0; i < ARRAY_SIZE(file->dht.segments); i++) {
        const unsigned char *stream = file->dht.segments[i];
        if (!stream)
            continue;

        stream += 2;
        const unsigned char *end = stream + segment_param_be16(stream);
        stream += 2;

        while (stream < end) {
            if (count >= ARRAY_SIZE(file->dht.Tc))
                va_die("too many dht");

            file->dht.Tc[count] = stream[0] >> 4;
            file->dht.Th[count] = stream[0] & 0xf;
            file->dht.Li[count] = &stream[1];

            if (stream + 1 + 16 > end)
                va_die("invalid dht");
            int sum = 0;
            for (int i = 0; i < 16; i++)
                sum += stream[1 + i];
            stream += 1 + 16;

            file->dht.Vij[count] = &stream[0];
            file->dht.Vij_sizes[count] = sum;

            stream += sum;
            if (stream > end)
                va_die("invalid dht");

            count++;
        }
    }
}

static void
jpegdec_test_parse_file_sof0(struct jpegdec_test *test)
{
    struct jpegdec_test_file *file = &test->file;
    const unsigned char *stream = file->sof0.segment + 4;

    file->sof0.P = stream[0];
    file->sof0.Y = segment_param_be16(&stream[1]);
    file->sof0.X = segment_param_be16(&stream[3]);
    file->sof0.Nf = stream[5];
    for (int i = 0; i < file->sof0.Nf; i++) {
        file->sof0.Ci[i] = stream[6 + 3 * i];
        file->sof0.Hi[i] = stream[6 + 3 * i + 1] >> 4;
        file->sof0.Vi[i] = stream[6 + 3 * i + 1] & 0xf;
        file->sof0.Tqi[i] = stream[6 + 3 * i + 2];
    }
}

static void
jpegdec_test_parse_file_dqt(struct jpegdec_test *test)
{
    struct jpegdec_test_file *file = &test->file;
    unsigned int count = 0;

    for (unsigned int i = 0; i < ARRAY_SIZE(file->dqt.segments); i++) {
        const unsigned char *stream = file->dqt.segments[i];
        if (!stream)
            continue;

        stream += 2;
        const unsigned char *end = stream + segment_param_be16(stream);
        stream += 2;

        while (stream < end) {
            if (count >= ARRAY_SIZE(file->dqt.Pq))
                va_die("too many dqt");

            file->dqt.Pq[count] = stream[0] >> 4;
            file->dqt.Tq[count] = stream[0] & 0xf;
            file->dqt.Qk[count] = &stream[1];

            stream += 1 + 64 * (1 + file->dqt.Pq[count]);
            if (stream > end)
                va_die("invalid dqt");

            count++;
        }
    }
}

static void
jpegdec_test_parse_file_segments(struct jpegdec_test *test)
{
    struct jpegdec_test_file *file = &test->file;

    const unsigned char *stream = file->ptr;
    if (file->size < 2 || stream[0] != 0xff || stream[1] != 0xd8)
        va_die("expect jpeg magic");

    const unsigned char *end = stream + file->size;
    while (stream < end && !file->eoi) {
        if (stream + 2 > end)
            va_die("incomplete jpeg file");

        if (stream[0] != 0xff)
            va_die("expect segment marker");

        const void **dst = NULL;
        switch (stream[1]) {
        case 0xc0: /* SOF0 */
            dst = &file->sof0.segment;
            break;
        case 0xc4: /* DHT */
            dst = &file->dht.segments[0];
            break;
        case 0xd8: /* SOI */
            dst = &file->soi;
            break;
        case 0xd9: /* EOI */
            dst = &file->eoi;
            break;
        case 0xda: /* SOS */
            dst = &file->sos.segment;
            break;
        case 0xdb: /* DQT */
            dst = &file->dqt.segments[0];
            break;
        case 0xdd: /* DRI */
            dst = &file->dri.segment;
            break;
        case 0xe0: /* APP0 */
        case 0xe1: /* APP1 */
        case 0xe2: /* APP2 */
            break;
        default:
            va_die("unknown marker 0x%02x%02x", stream[0], stream[1]);
            break;
        }

        if (dst) {
            /* duplicated segments */
            if (*dst) {
                if (dst == &file->dqt.segments[0] || dst == &file->dht.segments[0]) {
                    for (size_t i = 1; i < ARRAY_SIZE(file->dqt.segments); i++) {
                        if (!dst[i]) {
                            dst = &dst[i];
                            break;
                        }
                    }
                }

                if (*dst)
                    va_die("duplicated segment 0x%02x%02x", stream[0], stream[1]);
            }

            *dst = stream;
        }

        stream += 2;

        /* skip segment parameters */
        if (dst != &file->soi && dst != &file->eoi) {
            if (stream + 2 > end)
                va_die("incomplete jpeg segment");
            stream += segment_param_be16(stream);
        }

        if (dst == &file->sos.segment) {
            file->scan = stream;

            /* skip scan data */
            while (stream + 2 < end && (stream[0] != 0xff || stream[1] == 0x00))
                stream++;

            file->scan_size = (const void *)stream - file->scan;
        }
    }

    if (!file->soi || !file->dqt.segments[0] || !file->sof0.segment || !file->dht.segments[0] ||
        !file->sos.segment || !file->scan || !file->eoi)
        va_die("missing jpeg segments");
}

static void
jpegdec_test_parse_file(struct jpegdec_test *test)
{
    jpegdec_test_parse_file_segments(test);

    jpegdec_test_parse_file_dqt(test);
    jpegdec_test_parse_file_sof0(test);
    jpegdec_test_parse_file_dht(test);
    jpegdec_test_parse_file_sos(test);
    jpegdec_test_parse_file_dri(test);
}

static void
jpegdec_test_decode_file(struct jpegdec_test *test, const char *filename)
{
    struct va *va = &test->va;

    test->file.ptr = va_map_file(va, filename, &test->file.size);
    jpegdec_test_parse_file(test);

    jpegdec_test_prepare(test);
    jpegdec_test_decode(test);

    jpegdec_test_dump(test, "decoded.ppm");

    va_destroy_buffer(va, test->pic_param);
    va_destroy_buffer(va, test->iq_matrix);
    va_destroy_buffer(va, test->huffman_table);
    va_destroy_buffer(va, test->slice_param);
    va_destroy_buffer(va, test->slice_data);

    va_destroy_config(va, test->config);
    va_destroy_surface(va, test->surface);
    va_destroy_context(va, test->context);

    va_unmap_file(va, test->file.ptr, test->file.size);
    memset(&test->file, 0, sizeof(test->file));
}

static void
jpegdec_test_cleanup(struct jpegdec_test *test)
{
    struct va *va = &test->va;

    va_destroy_config(va, test->config);
    va_cleanup(va);
}

int
main(int argc, char **argv)
{
    struct jpegdec_test test = {
        .profile = VAProfileJPEGBaseline,
        .entrypoint = VAEntrypointVLD,
    };

    jpegdec_test_init(&test);

    for (int i = 1; i < argc; i++)
        jpegdec_test_decode_file(&test, argv[i]);

    jpegdec_test_cleanup(&test);

    return 0;
}
