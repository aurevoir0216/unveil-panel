// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
/* The driver stores 32 bytes, not necessarily a C string. Never read into the adjacent password. */
static inline void uv_ssid_to_text(char text[33],const uint8_t raw[32]){memcpy(text,raw,32);text[32]=0;}
static inline bool uv_ssid_matches(const char *text,const uint8_t raw[32]){size_t n=strnlen((const char*)raw,32);return strlen(text)==n&&!memcmp(text,raw,n);}
