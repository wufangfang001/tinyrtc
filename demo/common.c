/**
 * @file common.c
 * @brief Common utilities for demo applications
 */

#include "common.h"
#include "api/aosl.h"
#include <string.h>

static size_t demo_find_start_code(const uint8_t *data, size_t len, size_t *start_code_len)
{
    for (size_t i = 0; i + 3 < len; i++) {
        if (data[i] == 0x00 && data[i + 1] == 0x00) {
            if (data[i + 2] == 0x01) {
                *start_code_len = 3;
                return i;
            }
            if (i + 3 < len && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
                *start_code_len = 4;
                return i;
            }
        }
    }

    return len;
}

static bool demo_h264_is_vcl(uint8_t nalu_type)
{
    return nalu_type == 1 || nalu_type == 5;
}

static bool demo_h264_starts_new_au(uint8_t nalu_type)
{
    return nalu_type == 9 || nalu_type == 7 || nalu_type == 8 || nalu_type == 6;
}

static int demo_h264_append_unit(demo_h264_stream_t *stream, const uint8_t *data, size_t len)
{
    demo_h264_access_unit_t *units;
    demo_h264_access_unit_t *unit;

    units = (demo_h264_access_unit_t *)realloc(
        stream->units, (stream->count + 1) * sizeof(*stream->units));
    if (units == NULL) {
        return -1;
    }

    stream->units = units;
    unit = &stream->units[stream->count];
    unit->data = (uint8_t *)malloc(len);
    if (unit->data == NULL) {
        return -1;
    }

    memcpy(unit->data, data, len);
    unit->len = len;
    stream->count++;
    return 0;
}

char *demo_read_sdp(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *sdp = (char *)malloc(size + 1);
    if (!sdp) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(sdp, 1, size, f);
    sdp[read] = '\0';
    fclose(f);

    return sdp;
}

int demo_write_sdp(const char *path, const char *sdp)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        return -1;
    }

    fprintf(f, "%s", sdp);
    fclose(f);
    return 0;
}

int demo_init_aosl(void)
{
    /* Set log level to DEBUG so we can see all debug logs in terminal */
    aosl_set_log_level(AOSL_LOG_DEBUG);
    /* Disable stdout buffering to see logs immediately */
    setvbuf(stdout, NULL, _IONBF, 0);
    aosl_log(AOSL_LOG_INFO, "Initializing AOSL for demo...\n");
    /* AOSL is initialized automatically when linked */
    return 0;
}

void demo_exit_aosl(void)
{
    aosl_log(AOSL_LOG_INFO, "Exiting demo...\n");
}

int demo_h264_stream_load(const char *path, demo_h264_stream_t *stream)
{
    FILE *f;
    uint8_t *data = NULL;
    long file_size;
    size_t offset = 0;
    uint8_t *current_au = NULL;
    size_t current_au_len = 0;

    if (path == NULL || stream == NULL) {
        return -1;
    }

    memset(stream, 0, sizeof(*stream));

    f = fopen(path, "rb");
    if (!f) {
        return -1;
    }

    fseek(f, 0, SEEK_END);
    file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size <= 0) {
        fclose(f);
        return -1;
    }

    data = (uint8_t *)malloc((size_t)file_size);
    if (data == NULL) {
        fclose(f);
        return -1;
    }

    if (fread(data, 1, (size_t)file_size, f) != (size_t)file_size) {
        free(data);
        fclose(f);
        return -1;
    }
    fclose(f);

    while (offset < (size_t)file_size) {
        size_t start_code_len = 0;
        size_t start = demo_find_start_code(data + offset, (size_t)file_size - offset, &start_code_len);
        size_t next_start_code_len = 0;
        size_t end;
        uint8_t nalu_type;
        bool flush_before_append;
        uint8_t *new_buffer;

        if (start == (size_t)file_size - offset) {
            break;
        }
        start += offset;

        end = demo_find_start_code(data + start + start_code_len,
                                   (size_t)file_size - (start + start_code_len),
                                   &next_start_code_len);
        if (end == (size_t)file_size - (start + start_code_len)) {
            end = (size_t)file_size;
        } else {
            end += start + start_code_len;
        }

        if (end <= start + start_code_len) {
            break;
        }

        nalu_type = data[start + start_code_len] & 0x1F;
        flush_before_append = current_au_len > 0 &&
            (demo_h264_starts_new_au(nalu_type) || demo_h264_is_vcl(nalu_type));

        if (flush_before_append) {
            if (demo_h264_append_unit(stream, current_au, current_au_len) != 0) {
                free(current_au);
                demo_h264_stream_free(stream);
                free(data);
                return -1;
            }
            free(current_au);
            current_au = NULL;
            current_au_len = 0;
        }

        new_buffer = (uint8_t *)realloc(current_au, current_au_len + (end - start));
        if (new_buffer == NULL) {
            free(current_au);
            demo_h264_stream_free(stream);
            free(data);
            return -1;
        }
        current_au = new_buffer;
        memcpy(current_au + current_au_len, data + start, end - start);
        current_au_len += end - start;
        offset = end;
    }

    if (current_au_len > 0) {
        if (demo_h264_append_unit(stream, current_au, current_au_len) != 0) {
            free(current_au);
            demo_h264_stream_free(stream);
            free(data);
            return -1;
        }
        free(current_au);
    }

    free(data);
    return stream->count > 0 ? 0 : -1;
}

void demo_h264_stream_reset(demo_h264_stream_t *stream)
{
    if (stream != NULL) {
        stream->current_index = 0;
    }
}

void demo_h264_stream_free(demo_h264_stream_t *stream)
{
    if (stream == NULL) {
        return;
    }

    for (size_t i = 0; i < stream->count; i++) {
        free(stream->units[i].data);
    }
    free(stream->units);
    memset(stream, 0, sizeof(*stream));
}
