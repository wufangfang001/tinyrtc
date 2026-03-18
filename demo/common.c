/**
 * @file common.c
 * @brief Common utilities for demo applications
 */

#include "common.h"
#include "api/aosl.h"

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
    /* Set log level to INFO so we can see all logs in terminal */
    aosl_set_log_level(AOSL_LOG_INFO);
    aosl_log(AOSL_LOG_INFO, "Initializing AOSL for demo...\n");
    /* AOSL is initialized automatically when linked */
    return 0;
}

void demo_exit_aosl(void)
{
    aosl_log(AOSL_LOG_INFO, "Exiting demo...\n");
}
