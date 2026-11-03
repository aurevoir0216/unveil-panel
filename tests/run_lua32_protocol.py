#!/usr/bin/env python3
"""Exercise the real embedded JSON adapter against Lua compiled with float32."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--components',type=Path,required=True)
a=parser.parse_args()
repo=Path(__file__).resolve().parents[1]
component=repo/'firmware/mosaico-core/components/unveil_native'
lua=a.components/'espressif__lua'
cjson=a.components/'espressif__cjson'
work=Path(tempfile.mkdtemp(prefix='unveil-lua32-'))
(work/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n')
sources=[lua/name for name in re.findall(r'"(lua/[^" ]+\.c)"',(lua/'CMakeLists.txt').read_text())]
assert sources
command=['cc','-O1','-DLUA_32BITS=1','-DLUAI_MAXSTACK=10000','-I'+str(work),'-I'+str(lua/'lua'),'-I'+str(cjson/'cJSON'),'-I'+str(component),'-I'+str(component/'include'),str(repo/'tests/lua32_protocol.c'),str(component/'lua_module_json.c'),str(cjson/'cJSON/cJSON.c'),*[str(p) for p in sources],'-lm','-o',str(work/'protocol')]
subprocess.run(command,check=True,capture_output=True)
files = [*list((repo/'firmware/mosaico/app/scripts').glob('*.lua')),
         *list((repo/'firmware/mosaico-core/resources/apps/unveil/scripts').glob('*.lua')),
         repo/'firmware/mosaico-idea/resources_overrides/unveil.lua']
(work/'lua-files.txt').write_text('\n'.join(str(p) for p in files)+'\n')
subprocess.run([str(work/'protocol'),str(repo/'tests/lua32_protocol.lua'),str(component.parents[1]/'resources/apps/unveil/scripts/session_generation.lua')],check=True,env=dict(os.environ,UNVEIL_LUA_FILES=str(work/'lua-files.txt')))
