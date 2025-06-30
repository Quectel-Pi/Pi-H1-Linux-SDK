/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <stdlib.h>
#include <stddef.h>
#ifndef __WIFI_HAL_H__
#define __WIFI_HAL_H__

#ifdef __cplusplus
extern "C"
{
#endif
#include <stdint.h>

typedef enum {
    WIFI_SUCCESS = 0,
    WIFI_ERROR_NONE = 0,
    WIFI_ERROR_UNKNOWN = -1,
    WIFI_ERROR_UNINITIALIZED = -2,
    WIFI_ERROR_NOT_SUPPORTED = -3,
    WIFI_ERROR_NOT_AVAILABLE = -4,              // Not available right now, but try later
    WIFI_ERROR_INVALID_ARGS = -5,
    WIFI_ERROR_INVALID_REQUEST_ID = -6,
    WIFI_ERROR_TIMED_OUT = -7,
    WIFI_ERROR_TOO_MANY_REQUESTS = -8,          // Too many instances of this request
    WIFI_ERROR_OUT_OF_MEMORY = -9
} wifi_error;

typedef unsigned char byte;
typedef unsigned char u8;
typedef signed char s8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef int64_t s64;
typedef int wifi_request_id;
typedef int wifi_channel;                       // indicates channel frequency in MHz
typedef int wifi_rssi;
typedef byte mac_addr[6];
typedef byte oui[3];
typedef int64_t wifi_timestamp;                 // In microseconds (us)
typedef int64_t wifi_timespan;                  // In picoseconds  (ps)

struct wifi_info;
struct wifi_interface_info;
typedef struct wifi_info *wifi_handle;
typedef struct wifi_interface_info *wifi_interface_handle;

/* Initialize/Cleanup */

wifi_error wifi_initialize(wifi_handle *handle);
typedef void (*wifi_cleaned_up_handler) (wifi_handle handle);
void wifi_cleanup(wifi_handle handle, wifi_cleaned_up_handler handler);
void wifi_event_loop(wifi_handle handle);

/* Error handling */
void wifi_get_error_info(wifi_error err, const char **msg); // return a pointer to a static string
typedef int feature_set;

/* Configuration events */

typedef struct {
    void (*on_country_code_changed)(char code[2]);      // We can get this from supplicant too

    // More event handlers
} wifi_event_handler;

typedef struct {
        void (*on_rssi_threshold_breached)(wifi_request_id id, u8 *cur_bssid, s8 cur_rssi);
} wifi_rssi_event_handler;

wifi_error wifi_set_iface_event_handler(wifi_request_id id, wifi_interface_handle iface, wifi_event_handler eh);
wifi_error wifi_reset_iface_event_handler(wifi_request_id id, wifi_interface_handle iface);

wifi_error wifi_set_nodfs_flag(wifi_interface_handle handle, u32 nodfs);
#include "wifi_nan.h"
#ifdef __cplusplus
}
#endif
#endif
