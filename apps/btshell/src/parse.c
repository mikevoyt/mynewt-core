/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include "console/console.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_eddystone.h"
#include "parse/parse.h"
#include "cmd.h"
#include "btshell.h"

#define CMD_MAX_ARGS        16

static char *cmd_args[CMD_MAX_ARGS][2];
static int cmd_num_args;

int
parse_arg_find_idx(const char *key)
{
    int i;

    for (i = 0; i < cmd_num_args; i++) {
        if (strcmp(cmd_args[i][0], key) == 0) {
            return i;
        }
    }

    return -1;
}

char *
parse_arg_peek(const char *key)
{
    int i;

    for (i = 0; i < cmd_num_args; i++) {
        if (strcmp(cmd_args[i][0], key) == 0) {
            return cmd_args[i][1];
        }
    }

    return NULL;
}

char *
parse_arg_extract(const char *key)
{
    int i;

    for (i = 0; i < cmd_num_args; i++) {
        if (strcmp(cmd_args[i][0], key) == 0) {
            /* Erase parameter. */
            cmd_args[i][0][0] = '\0';

            return cmd_args[i][1];
        }
    }

    return NULL;
}

long
parse_long_bounds(char *sval, long min, long max, int *out_status)
{
    return parse_ll_bounds(sval, min, max, out_status);
}

long
parse_arg_long_bounds_peek(char *name, long min, long max, int *out_status)
{
    char *sval;

    sval = parse_arg_peek(name);
    if (sval == NULL) {
        *out_status = ENOENT;
        return 0;
    }
    return parse_long_bounds(sval, min, max, out_status);
}

long
parse_arg_long_bounds(char *name, long min, long max, int *out_status)
{
    char *sval;

    sval = parse_arg_extract(name);
    if (sval == NULL) {
        *out_status = ENOENT;
        return 0;
    }
    return parse_long_bounds(sval, min, max, out_status);
}

long
parse_arg_long_bounds_dflt(char *name, long min, long max,
                              long dflt, int *out_status)
{
    long val;
    int rc;

    val = parse_arg_long_bounds(name, min, max, &rc);
    if (rc == ENOENT) {
        rc = 0;
        val = dflt;
    }

    *out_status = rc;

    return val;
}

uint64_t
parse_arg_uint64_bounds(char *name, uint64_t min, uint64_t max, int *out_status)
{
    char *sval;

    sval = parse_arg_extract(name);
    if (sval == NULL) {
        *out_status = ENOENT;
        return 0;
    }

    return parse_ull_bounds(sval, min, max, out_status);
}

long
parse_arg_long(char *name, int *out_status)
{
    return parse_arg_long_bounds(name, LONG_MIN, LONG_MAX, out_status);
}

uint8_t
parse_arg_bool(char *name, int *out_status)
{
    return parse_arg_long_bounds(name, 0, 1, out_status);
}

uint8_t
parse_arg_bool_dflt(char *name, uint8_t dflt, int *out_status)
{
    return parse_arg_long_bounds_dflt(name, 0, 1, dflt, out_status);
}

uint8_t
parse_arg_uint8(char *name, int *out_status)
{
    return parse_arg_long_bounds(name, 0, UINT8_MAX, out_status);
}

uint16_t
parse_arg_uint16(char *name, int *out_status)
{
    return parse_arg_long_bounds(name, 0, UINT16_MAX, out_status);
}

uint16_t
parse_arg_uint16_peek(char *name, int *out_status)
{
    return parse_arg_long_bounds_peek(name, 0, UINT16_MAX, out_status);
}

uint32_t
parse_arg_uint32(char *name, int *out_status)
{
    return parse_arg_uint64_bounds(name, 0, UINT32_MAX, out_status);
}

uint64_t
parse_arg_uint64(char *name, int *out_status)
{
    return parse_arg_uint64_bounds(name, 0, UINT64_MAX, out_status);
}

uint8_t
parse_arg_uint8_dflt(char *name, uint8_t dflt, int *out_status)
{
    uint8_t val;
    int rc;

    val = parse_arg_uint8(name, &rc);
    if (rc == ENOENT) {
        val = dflt;
        rc = 0;
    }

    *out_status = rc;
    return val;
}

uint16_t
parse_arg_uint16_dflt(char *name, uint16_t dflt, int *out_status)
{
    uint16_t val;
    int rc;

    val = parse_arg_uint16(name, &rc);
    if (rc == ENOENT) {
        val = dflt;
        rc = 0;
    }

    *out_status = rc;
    return val;
}

uint32_t
parse_arg_uint32_dflt(char *name, uint32_t dflt, int *out_status)
{
    uint32_t val;
    int rc;

    val = parse_arg_uint32(name, &rc);
    if (rc == ENOENT) {
        val = dflt;
        rc = 0;
    }

    *out_status = rc;
    return val;
}

const struct kv_pair *
parse_kv_find(const struct kv_pair *kvs, char *name)
{
    const struct kv_pair *kv;
    int i;

    for (i = 0; kvs[i].key != NULL; i++) {
        kv = kvs + i;
        if (strcmp(name, kv->key) == 0) {
            return kv;
        }
    }

    return NULL;
}

int
parse_arg_kv(char *name, const struct kv_pair *kvs, int *out_status)
{
    const struct kv_pair *kv;
    char *sval;

    sval = parse_arg_extract(name);
    if (sval == NULL) {
        *out_status = ENOENT;
        return -1;
    }

    kv = parse_kv_find(kvs, sval);
    if (kv == NULL) {
        *out_status = EINVAL;
        return -1;
    }

    *out_status = 0;
    return kv->val;
}

int
parse_arg_kv_dflt(char *name, const struct kv_pair *kvs, int def_val,
                     int *out_status)
{
    int val;
    int rc;

    val = parse_arg_kv(name, kvs, &rc);
    if (rc == ENOENT) {
        rc = 0;
        val = def_val;
    }

    *out_status = rc;

    return val;
}

static void
parse_reverse_bytes(uint8_t *bytes, int len)
{
    uint8_t tmp;
    int i;

    for (i = 0; i < len / 2; i++) {
        tmp = bytes[i];
        bytes[i] = bytes[len - i - 1];
        bytes[len - i - 1] = tmp;
    }
}

int
parse_arg_mac(char *name, uint8_t *dst)
{
    int rc;

    rc = parse_byte_stream_exact_length(name, dst, 6);
    if (rc != 0) {
        return rc;
    }

    parse_reverse_bytes(dst, 6);

    return 0;
}

int
parse_arg_uuid(char *str, ble_uuid_any_t *uuid)
{
    uint16_t uuid16;
    uint8_t val[16];
    int len;
    int rc;

    uuid16 = parse_arg_uint16_peek(str, &rc);
    switch (rc) {
    case ENOENT:
        parse_arg_extract(str);
        return ENOENT;

    case 0:
        len = 2;
        val[0] = uuid16;
        val[1] = uuid16 >> 8;
        parse_arg_extract(str);
        break;

    default:
        len = 16;
        rc = parse_byte_stream_exact_length(str, val, 16);
        if (rc != 0) {
            return EINVAL;
        }
        parse_reverse_bytes(val, 16);
        break;
    }

    rc = ble_uuid_init_from_buf(uuid, val, len);
    if (rc != 0) {
        return EINVAL;
    } else {
        return 0;
    }
}

int
parse_arg_all(int argc, char **argv)
{
    char *key;
    char *val;
    int i;

    cmd_num_args = 0;

    for (i = 0; i < argc; i++) {
        key = strtok(argv[i], "=");
        val = strtok(NULL, "=");

        if (key != NULL && val != NULL) {
            if (strlen(key) == 0) {
                console_printf("Error: invalid argument: %s\n", argv[i]);
                return -1;
            }

            if (cmd_num_args >= CMD_MAX_ARGS) {
                console_printf("Error: too many arguments");
                return -1;
            }

            cmd_args[cmd_num_args][0] = key;
            cmd_args[cmd_num_args][1] = val;
            cmd_num_args++;
        }
    }

    return 0;
}

int
parse_eddystone_url(char *full_url, uint8_t *out_scheme, char *out_body,
                    uint8_t *out_body_len, uint8_t *out_suffix)
{
    static const struct {
        char *s;
        uint8_t scheme;
    } schemes[] = {
        { "http://www.", BLE_EDDYSTONE_URL_SCHEME_HTTP_WWW },
        { "https://www.", BLE_EDDYSTONE_URL_SCHEME_HTTPS_WWW },
        { "http://", BLE_EDDYSTONE_URL_SCHEME_HTTP },
        { "https://", BLE_EDDYSTONE_URL_SCHEME_HTTPS },
    };

    static const struct {
        char *s;
        uint8_t code;
    } suffixes[] = {
        { ".com/", BLE_EDDYSTONE_URL_SUFFIX_COM_SLASH },
        { ".org/", BLE_EDDYSTONE_URL_SUFFIX_ORG_SLASH },
        { ".edu/", BLE_EDDYSTONE_URL_SUFFIX_EDU_SLASH },
        { ".net/", BLE_EDDYSTONE_URL_SUFFIX_NET_SLASH },
        { ".info/", BLE_EDDYSTONE_URL_SUFFIX_INFO_SLASH },
        { ".biz/", BLE_EDDYSTONE_URL_SUFFIX_BIZ_SLASH },
        { ".gov/", BLE_EDDYSTONE_URL_SUFFIX_GOV_SLASH },
        { ".com", BLE_EDDYSTONE_URL_SUFFIX_COM },
        { ".org", BLE_EDDYSTONE_URL_SUFFIX_ORG },
        { ".edu", BLE_EDDYSTONE_URL_SUFFIX_EDU },
        { ".net", BLE_EDDYSTONE_URL_SUFFIX_NET },
        { ".info", BLE_EDDYSTONE_URL_SUFFIX_INFO },
        { ".biz", BLE_EDDYSTONE_URL_SUFFIX_BIZ },
        { ".gov", BLE_EDDYSTONE_URL_SUFFIX_GOV },
    };

    char *prefix;
    char *suffix;
    int full_url_len;
    int prefix_len;
    int suffix_len;
    int suffix_idx;
    int rc;
    int i;

    full_url_len = strlen(full_url);

    rc = BLE_HS_EINVAL;
    for (i = 0; i < sizeof schemes / sizeof schemes[0]; i++) {
        prefix = schemes[i].s;
        prefix_len = strlen(schemes[i].s);

        if (full_url_len >= prefix_len &&
            memcmp(full_url, prefix, prefix_len) == 0) {

            *out_scheme = i;
            rc = 0;
            break;
        }
    }
    if (rc != 0) {
        return rc;
    }

    rc = BLE_HS_EINVAL;
    for (i = 0; i < sizeof suffixes / sizeof suffixes[0]; i++) {
        suffix = suffixes[i].s;
        suffix_len = strlen(suffixes[i].s);

        suffix_idx = full_url_len - suffix_len;
        if (suffix_idx >= prefix_len &&
            memcmp(full_url + suffix_idx, suffix, suffix_len) == 0) {

            *out_suffix = i;
            rc = 0;
            break;
        }
    }
    if (rc != 0) {
        *out_suffix = BLE_EDDYSTONE_URL_SUFFIX_NONE;
        *out_body_len = full_url_len - prefix_len;
    } else {
        *out_body_len = full_url_len - prefix_len - suffix_len;
    }

    memcpy(out_body, full_url + prefix_len, *out_body_len);

    return 0;
}
