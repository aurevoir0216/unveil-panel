local json=require('json')
local generations=dofile(GENERATION_MODULE)
local checked=0
for _,clock in ipairs({0,1,119999,120000,120001,2147483647}) do
 local seed=generations.seed(clock)
 assert(seed>=0 and seed<8000000)
 for offset=0,128 do
  local value=seed+offset
  local actual=json.decode(json.encode({request_generation=value,chart_generation=value,history_generation=value}))
  assert(actual.request_generation==value and actual.chart_generation==value and actual.history_generation==value,'float32 generation lost precision')
  checked=checked+1
 end
end
-- Confirm the test actually exercises the float32 failure that motivated bounded IDs.
local old=734531001
assert(json.decode(json.encode({generation=old})).generation~=old)
print('LUA32_PROTOCOL_OK cases='..checked)

local list=os.getenv('UNVEIL_LUA_FILES')
if list then
 local count=0
 for path in io.lines(list) do local code,error=loadfile(path);assert(code,error);count=count+1 end
 print('LUA32_SYNTAX_OK files='..count)
end

-- Actual shared visual start gate: no acquisition values are substituted.
local Gate=dofile(GENERATION_MODULE:gsub('session_generation.lua$','scan_gate.lua'))
local p={ok=true,generation=7,sequence=10,submitted=90,transferred=90,presented=90}
local f=Gate.new(true)
assert(Gate.step(f,false,p)==nil)
assert(Gate.step(f,true,p)=='paint')
assert(Gate.step(f,true,p)==nil)
p.sequence=11;p.submitted=91
assert(Gate.step(f,true,p)==nil)
p.transferred=91
assert(Gate.step(f,true,p)==nil)
p.presented=91
assert(Gate.step(f,true,p)=='start' and f.haptic)
-- Motion after a candidate frame invalidates it; a fresh static frame is needed.
f=Gate.new(false);assert(Gate.step(f,true,p)=='paint')
assert(Gate.step(f,false,p)==nil)
assert(Gate.step(f,true,p)=='paint')
p.sequence=12;p.submitted=92;p.transferred=92;p.presented=92
assert(Gate.step(f,true,p)=='start' and not f.haptic)
f=Gate.new();assert(Gate.step(f,true,p)=='paint')
p.generation=8;assert(Gate.step(f,true,p)=='paint')
p.ok=false;assert(Gate.step(f,true,p)==nil)
p.ok=true;assert(Gate.step(f,true,p)=='paint')
print('SCAN_START_GATE_OK cases=14')
