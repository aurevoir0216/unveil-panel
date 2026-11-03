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
