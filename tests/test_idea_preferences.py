#!/usr/bin/env python3
"""Test the actual preference snapshot code with real cJSON and an NVS mock."""
import argparse
import ctypes
import json
from pathlib import Path
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--components', type=Path, required=True)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
source = repo / 'firmware/mosaico-core/components/unveil_native'
cjson = args.components / 'espressif__cjson/cJSON'
with tempfile.TemporaryDirectory(prefix='unveil-preferences-') as temp:
    work = Path(temp)
    config, restore = work / 'config.json', work / 'restore.tmp'
    # Change only filesystem locations; compile the production algorithm.
    code = (source / 'uv_preferences.c').read_text()
    code = code.replace('"/data/apps/unveil/config.json"', json.dumps(str(config)))
    code = code.replace('"/data/apps/unveil/config.restore.tmp"', json.dumps(str(restore)))
    (work / 'preferences.c').write_text(code)
    (work / 'esp_err.h').write_text('''#pragma once
#include <stdbool.h>
typedef int esp_err_t;
enum {ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NO_MEM=0x101, ESP_ERR_INVALID_SIZE=0x104,
 ESP_ERR_NOT_FOUND=0x105, ESP_ERR_INVALID_RESPONSE=0x108, ESP_ERR_NVS_NOT_FOUND=0x1102};
''')
    (work / 'nvs.h').write_text('''#pragma once
#include <stddef.h>
#include "esp_err.h"
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
esp_err_t nvs_open(const char *,int,nvs_handle_t *);
esp_err_t nvs_get_blob(nvs_handle_t,const char *,void *,size_t *);
esp_err_t nvs_set_blob(nvs_handle_t,const char *,const void *,size_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
''')
    (work / 'nvs.c').write_text('''#include "nvs.h"
#include <string.h>
static unsigned char blob[4096];static size_t length;static int writes;
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *out){
 if(strcmp(name,"uv_settings"))return ESP_FAIL;
 if(!mode&&!length)return ESP_ERR_NVS_NOT_FOUND;*out=1;return ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size){
 (void)h;if(strcmp(key,"config"))return ESP_FAIL;if(!length)return ESP_ERR_NVS_NOT_FOUND;
 if(!out){*size=length;return ESP_OK;}if(*size<length){*size=length;return ESP_ERR_INVALID_SIZE;}
 memcpy(out,blob,length);*size=length;return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t size){
 (void)h;if(strcmp(key,"config")||size>sizeof(blob))return ESP_FAIL;
 memcpy(blob,data,size);length=size;++writes;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;return ESP_OK;}
void nvs_close(nvs_handle_t h){(void)h;}
int mock_writes(void){return writes;}
void mock_corrupt(void){memcpy(blob,"broken",6);length=6;}
''')
    library = work / ('test.dylib' if sys.platform == 'darwin' else 'test.so')
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra',
                    '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-fPIC',
                    '-I'+str(work), '-I'+str(source / 'include'), '-I'+str(cjson),
                    str(work / 'preferences.c'), str(work / 'nvs.c'), str(cjson / 'cJSON.c'),
                    '-lm', '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    save = lib.uv_preferences_save_if_config
    save.argtypes = [ctypes.c_char_p]
    restore_config = lib.uv_preferences_restore
    original = b'{"pi_url":"https://example.invalid","dark":true,"capture_seconds":2}'
    config.write_bytes(original)
    assert restore_config() == 0 and lib.mock_writes() == 1
    assert save(str(config).encode()) == 0 and lib.mock_writes() == 1
    config.write_bytes(b'{"pi_url":"","dark":false}')
    assert restore_config() == 0 and config.read_bytes() == original
    config.unlink()
    assert restore_config() == 0 and config.read_bytes() == original
    updated = b'{"pi_url":"http://example.invalid","dark":false}'
    config.write_bytes(updated)
    assert save(str(config).encode()) == 0 and lib.mock_writes() == 2
    config.write_bytes(b'{"pi_url":""}')
    assert restore_config() == 0 and config.read_bytes() == updated
    assert save(str(work / 'ui-status.json').encode()) == 0 and lib.mock_writes() == 2
    for bad in [b'[]', b'{broken}', b'{"a":"'+b'x'*2048+b'"}', b'{}\x00secret']:
        config.write_bytes(bad)
        assert save(str(config).encode()) != 0 and lib.mock_writes() == 2
    config.write_bytes(updated)
    lib.mock_corrupt()
    assert restore_config() != 0 and config.read_bytes() == updated
    assert not restore.exists()
    print('IDEA_PREFERENCES_OK resource-reset, missing-file, settings-change, deduplication, status-exclusion, corrupt/oversized-rejection')
