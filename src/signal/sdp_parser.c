/**
 * @file sdp_parser.c
 * @brief SDP (Session Description Protocol) parser implementation
 *
 * Parses SDP text into structured session description.
 * Follows RFC 4566 for WebRTC usage.
 *
 * Copyright (c) 2026
 * Licensed under MIT License
 */

#include "common.h"
#include "sdp_internal.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* =============================================================================
 * Local helper functions
 * ========================================================================== */

/**
 * @brief Skip whitespace at start of string
 */
static const char *skip_whitespace(const char *p)
{
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

/**
 * @brief Find end of line
 */
static const char *find_eol(const char *p)
{
    while (*p != '\0' && *p != '\n' && *p != '\r') {
        p++;
    }
    return p;
}

/**
 * @brief Copy until next separator or end of line
 */
static int copy_until(char *dst, size_t dst_len, const char **start, char sep)
{
    const char *p = *start;
    p = skip_whitespace(p);
    size_t i = 0;
    while (*p != '\0' && *p != '\n' && *p != '\r' && *p != sep && i < dst_len - 1) {
        if (*p != ' ' || i > 0) { /* Skip leading space */
            dst[i++] = *p;
        }
        p++;
    }
    dst[i] = '\0';
    /* Skip trailing space */
    while (i > 0 && dst[i - 1] == ' ') {
        i--;
        dst[i] = '\0';
    }
    *start = p;
    return (int)i;
}

/**
 * @brief Parse integer from string
 */
static int parse_int(const char *str, int *out)
{
    char *end;
    long val = strtol(str, &end, 10);
    if (end == str) {
        return -1;
    }
    *out = (int)val;
    return 0;
}

/**
 * @brief Parse unsigned 64-bit integer
 */
static int parse_uint64(const char *str, uint64_t *out)
{
    char *end;
    unsigned long long val = strtoull(str, &end, 10);
    if (end == str) {
        return -1;
    }
    *out = (uint64_t)val;
    return 0;
}

/**
 * @brief Parse candidate attribute (a=candidate:... )
 */
static int parse_candidate(const char *line, sdp_candidate_t *cand)
{
    /* candidate format: foundation component-id priority ip port transport typ [raddr rport] tcptype */
    int component_id;

    /* foundation */
    copy_until(cand->foundation, sizeof(cand->foundation), &line, ' ');
    if (cand->foundation[0] == '\0') {
        return -1;
    }

    /* component-id */
    int comp;
    char comp_str[16];
    copy_until(comp_str, sizeof(comp_str), &line, ' ');
    if (parse_int(comp_str, &comp) != 0) {
        return -1;
    }
    (void)comp; /* We only care about component 1 for RTP */

    /* priority */
    char prio_str[32];
    copy_until(prio_str, sizeof(prio_str), &line, ' ');
    unsigned long prio;
    if (parse_int(prio_str, (int *)&prio) != 0) {
        return -1;
    }
    cand->priority = (uint32_t)prio;

    /* IP address */
    copy_until(cand->ip, sizeof(cand->ip), &line, ' ');
    if (cand->ip[0] == '\0') {
        return -1;
    }
    cand->is_ipv6 = strchr(cand->ip, ':') != NULL;

    /* port */
    char port_str[16];
    copy_until(port_str, sizeof(port_str), &line, ' ');
    int port;
    if (parse_int(port_str, &port) != 0) {
        return -1;
    }
    cand->port = (uint16_t)port;

    /* protocol (udp/tcp) */
    copy_until(cand->protocol, sizeof(cand->protocol), &line, ' ');
    if (cand->protocol[0] == '\0') {
        strcpy(cand->protocol, "udp");
    }

    /* type (host/srflx/relay) */
    copy_until(cand->type, sizeof(cand->type), &line, ' ');
    if (cand->type[0] == '\0') {
        return -1;
    }

    return 0;
}

/**
 * @brief Parse fingerprint attribute (a=fingerprint:... )
 */
static int parse_fingerprint(const char *line, char *type, size_t type_len, char *fp, size_t fp_len)
{
    copy_until(type, type_len, &line, ' ');
    if (type[0] == '\0') {
        return -1;
    }
    /* Skip space and copy the rest */
    line = skip_whitespace((char *)line);
    size_t i = 0;
    while (*line != '\0' && *line != '\n' && *line != '\r' && i < fp_len - 1) {
        fp[i++] = *line++;
    }
    fp[i] = '\0';
    return 0;
}

/* =============================================================================
 * Public API implementation
 * ========================================================================== */

void sdp_session_init(sdp_session_t *session)
{
    memset(session, 0, sizeof(*session));
    session->version = 0;
    session->start_time = 0;
    session->stop_time = 0;
}

tinyrtc_error_t sdp_parse(const char *text, sdp_session_t *session)
{
    TINYRTC_CHECK_NULL_RET(TINYRTC_ERROR_INVALID_ARG, text);
    TINYRTC_CHECK_NULL_RET(TINYRTC_ERROR_INVALID_ARG, session);

    sdp_session_init(session);

    const char *p = text;
    int line_num = 0;
    while (*p != '\0') {
        if (*p == '\n' || *p == '\r') {
            p++;
            continue;
        }

        line_num++;
        char type = *p;
        p++;
        if (*p != '=') {
            /* Invalid line, skip */
            TINYRTC_LOG_WARN("Invalid SDP line (no = after type char) at line %d", line_num);
            p = find_eol(p);
            p++;
            continue;
        }
        p++;

        TINYRTC_LOG_DEBUG("SDP parsing line %d type '%c'", line_num, type);

        switch (type) {
            case 'v': /* Version */
                parse_int(p, &session->version);
                /* parse_int doesn't advance p past the number, we need to skip to end of line */
                p = find_eol(p);
                break;

            case 'o': /* Origin */
            {
                copy_until(session->username, sizeof(session->username), &p, ' ');
                uint64_t sess_id, sess_ver;
                char buf[64];
                copy_until(buf, sizeof(buf), &p, ' ');
                parse_uint64(buf, &sess_id);
                session->session_id = sess_id;
                copy_until(buf, sizeof(buf), &p, ' ');
                parse_uint64(buf, &sess_ver);
                session->session_version = sess_ver;
                copy_until(session->network_type, sizeof(session->network_type), &p, ' ');
                copy_until(session->address_type, sizeof(session->address_type), &p, ' ');
                copy_until(session->unicast_address, sizeof(session->unicast_address), &p, '\n');
                break;
            }

            case 's': /* Session name */
                copy_until(session->session_name, sizeof(session->session_name), &p, '\n');
                break;

            case 't': /* Timing */
            {
                char buf[64];
                copy_until(buf, sizeof(buf), &p, ' ');
                parse_uint64(buf, &session->start_time);
                copy_until(buf, sizeof(buf), &p, '\n');
                parse_uint64(buf, &session->stop_time);
                break;
            }

            case 'a': /* Attribute */
            {
                char attr_name[64];
                const char *line_start = p;
                copy_until(attr_name, sizeof(attr_name), &p, ':');
                if (strcmp(attr_name, "ice-ufrag") == 0) {
                    copy_until(session->ice_ufrag, sizeof(session->ice_ufrag), &p, '\n');
                } else if (strcmp(attr_name, "ice-pwd") == 0) {
                    copy_until(session->ice_pwd, sizeof(session->ice_pwd), &p, '\n');
                } else if (strcmp(attr_name, "fingerprint") == 0) {
                    parse_fingerprint(p, session->fingerprint_type,
                                     sizeof(session->fingerprint_type),
                                     session->fingerprint,
                                     sizeof(session->fingerprint));
                } else if (strcmp(attr_name, "candidate") == 0) {
                    if (session->num_candidates < SDP_MAX_CANDIDATES) {
                        sdp_candidate_t *cand = &session->candidates[session->num_candidates];
                        if (parse_candidate(p, cand) == 0) {
                            session->num_candidates++;
                        }
                    }
                } else if (strcmp(attr_name, "setup") == 0) {
                    copy_until(session->dtls_setup, sizeof(session->dtls_setup), &p, '\n');
                } else if (strcmp(attr_name, "mid") == 0) {
                    /* a=mid:<mid-value> - set mid for current media track */
                    if (session->num_media > 0) {
                        char mid_value[32];
                        /* Skip the ':' (we're at it after copy_until(':') */
                        if (*p == ':') p++;
                        copy_until(mid_value, sizeof(mid_value), &p, '\n');
                        /* Set mid for the last added media track */
                        strncpy(session->media[session->num_media - 1].mid, mid_value, 
                               sizeof(session->media[session->num_media - 1].mid) - 1);
                        session->media[session->num_media - 1].mid[sizeof(session->media[session->num_media - 1].mid) - 1] = '\0';
                    }
                } else if (strcmp(attr_name, "rtpmap") == 0) {
                    /* Parse rtpmap: a=rtpmap:<payload-type> <codec-name>/<clock-rate>[/<channels>] */
                    int pt;
                    char codec_info[128];
                    char codec_name[64];
                    char clock_rate_str[32];
                    uint32_t clock_rate;
                    int channels = 1;

                    /* Skip the ':' */
                    if (*p == ':') p++;

                    /* Parse payload type */
                    if (parse_int(p, &pt) == 0) {
                        /* Skip to space after payload type */
                        while (*p != ' ' && *p != '\0' && *p != '\n') p++;
                        if (*p == ' ') p++;

                        /* Format: <codec-name> <clock-rate>[/<channels>] */
                        /* First, copy codec name (up to space) */
                        copy_until(codec_name, sizeof(codec_name), &p, ' ');

                        /* Then copy clock rate and channels info */
                        char rate_channels[64];
                        copy_until(rate_channels, sizeof(rate_channels), &p, '\n');

                        /* Split rate_channels by / */
                        char *slash = strchr(rate_channels, '/');
                        if (slash) {
                            *slash = '\0';
                            clock_rate = (uint32_t)atoi(rate_channels);
                            channels = atoi(slash + 1);
                        } else {
                            clock_rate = (uint32_t)atoi(rate_channels);
                        }

                        /* Find matching media track and update codec info */
                        for (int i = 0; i < session->num_media; i++) {
                            if (session->media[i].payload_type == pt) {
                                /* Map codec name to codec_id */
                                if (strcasecmp(codec_name, "H264") == 0) {
                                    session->media[i].codec_id = TINYRTC_CODEC_H264;
                                } else if (strcasecmp(codec_name, "OPUS") == 0) {
                                    session->media[i].codec_id = TINYRTC_CODEC_OPUS;
                                } else if (strcasecmp(codec_name, "PCMA") == 0) {
                                    session->media[i].codec_id = TINYRTC_CODEC_PCMA;
                                } else if (strcasecmp(codec_name, "PCMU") == 0) {
                                    session->media[i].codec_id = TINYRTC_CODEC_PCMU;
                                } else if (strcasecmp(codec_name, "G722") == 0) {
                                    session->media[i].codec_id = TINYRTC_CODEC_G722;
                                }
                                if (clock_rate > 0) {
                                    session->media[i].clock_rate = clock_rate;
                                }
                                session->media[i].channels = channels;
                                TINYRTC_LOG_DEBUG("rtpmap: pt=%d codec=%s clock_rate=%u channels=%d",
                                    pt, tinyrtc_codec_get_name(session->media[i].codec_id),
                                    clock_rate, channels);
                                break;
                            }
                        }
                    }
                }
                /* Ignore other attributes for now */
                break;
            }

            case 'm': /* Media description */
            {
                if (session->num_media >= SDP_MAX_MEDIA) {
                    TINYRTC_LOG_WARN("Too many media tracks in SDP, skipping");
                    break;
                }

                sdp_media_t *media = &session->media[session->num_media];
                memset(media, 0, sizeof(*media));

                char media_type[16];
                char port_str[16];
                char proto_str[16];
                char payload_type_str[16];

                copy_until(media_type, sizeof(media_type), &p, ' ');
                copy_until(port_str, sizeof(port_str), &p, ' ');
                copy_until(proto_str, sizeof(proto_str), &p, ' ');

                parse_int(port_str, &media->port);

                if (strcmp(media_type, "audio") == 0) {
                    media->kind = TINYRTC_TRACK_KIND_AUDIO;
                } else if (strcmp(media_type, "video") == 0) {
                    media->kind = TINYRTC_TRACK_KIND_VIDEO;
                } else {
                    /* Unknown media type, skip */
                    TINYRTC_LOG_WARN("Unknown media type '%s', skipping", media_type);
                    break;
                }

                /* Parse payload type - should be at end of line or followed by space */
                char *end_ptr;
                long pt = strtol(p, &end_ptr, 10);
                if (end_ptr != p) {
                    media->payload_type = (int)pt;
                    p = end_ptr;
                }

                /* Set default codec_id based on payload type
                 * Note: will be updated later if a=rtpmap is found
                 */
                if (media->payload_type == 0) {
                    media->codec_id = TINYRTC_CODEC_PCMU;
                    media->clock_rate = 8000;
                    media->channels = 1;
                } else if (media->payload_type == 8) {
                    media->codec_id = TINYRTC_CODEC_PCMA;
                    media->clock_rate = 8000;
                    media->channels = 1;
                } else if (media->payload_type == 9) {
                    media->codec_id = TINYRTC_CODEC_G722;
                    media->clock_rate = 8000;
                    media->channels = 1;
                } else if (media->payload_type == 100) {
                    media->codec_id = TINYRTC_CODEC_H264;
                    media->clock_rate = 90000;
                    media->channels = 1;
                } else if (media->payload_type == 111) {
                    media->codec_id = TINYRTC_CODEC_OPUS;
                    media->clock_rate = 48000;
                    media->channels = 2;
                }

                /* Default directions */
                media->direction_send = true;
                media->direction_recv = true;

                session->num_media++;
                break;
            }

            default:
                /* Ignore other lines (c=, b=, etc.) for now */
                break;
        }

        /* Go to next line - all lines get the same treatment */
        p = find_eol(p);
        if (*p != '\0') {
            p++;
            /* Handle \r\n */
            if (*p == '\n') {
                p++;
            }
        }
    }

    /* Basic validation */
    if (session->version == 0) {
        TINYRTC_LOG_DEBUG("SDP parsed with %d media tracks, %d candidates",
            session->num_media, session->num_candidates);
        return TINYRTC_OK;
    }

    TINYRTC_LOG_ERROR("SDP parse failed: invalid version (must be 0 per RFC 4566)");
    return TINYRTC_ERROR;
}

sdp_media_t *sdp_find_media_by_mid(sdp_session_t *session, const char *mid)
{
    for (int i = 0; i < session->num_media; i++) {
        if (strcmp(session->media[i].mid, mid) == 0) {
            return &session->media[i];
        }
    }
    return NULL;
}

int sdp_add_media(sdp_session_t *session, const tinyrtc_track_config_t *track_config)
{
    if (session->num_media >= SDP_MAX_MEDIA) {
        return -1;
    }

    sdp_media_t *media = &session->media[session->num_media];
    memset(media, 0, sizeof(*media));

    media->kind = track_config->kind;
    if (track_config->payload_type != 0) {
        media->payload_type = track_config->payload_type;
    } else {
        media->payload_type = tinyrtc_codec_get_default_payload(track_config->codec_id);
    }
    if (track_config->clock_rate != 0) {
        media->clock_rate = track_config->clock_rate;
    } else {
        media->clock_rate = tinyrtc_codec_get_clock_rate(track_config->codec_id);
    }
    if (track_config->channels != 0) {
        media->channels = track_config->channels;
    } else {
        media->channels = tinyrtc_codec_get_channels(track_config->codec_id);
    }
    media->codec_id = track_config->codec_id;
    media->port = 9; /* 9 is used for SDP when port is not known yet */
    strncpy(media->mid, track_config->mid, sizeof(media->mid) - 1);
    media->direction_send = true;
    media->direction_recv = true;

    session->num_media++;

    return 0;
}

int sdp_add_candidate(sdp_session_t *session, const tinyrtc_ice_candidate_t *candidate)
{
    if (session->num_candidates >= SDP_MAX_CANDIDATES) {
        return -1;
    }

    sdp_candidate_t *sdp_cand = &session->candidates[session->num_candidates];
    memset(sdp_cand, 0, sizeof(*sdp_cand));

    strncpy(sdp_cand->foundation, candidate->foundation, sizeof(sdp_cand->foundation) - 1);
    strncpy(sdp_cand->ip, candidate->ip, sizeof(sdp_cand->ip) - 1);
    sdp_cand->port = candidate->port;
    sdp_cand->priority = candidate->priority;
    strncpy(sdp_cand->type, candidate->type, sizeof(sdp_cand->type) - 1);
    strncpy(sdp_cand->protocol, candidate->protocol, sizeof(sdp_cand->protocol) - 1);
    sdp_cand->is_ipv6 = candidate->is_ipv6;

    session->num_candidates++;

    return 0;
}
