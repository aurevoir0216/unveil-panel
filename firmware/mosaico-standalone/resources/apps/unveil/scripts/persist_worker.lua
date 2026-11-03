local storage,system=require('storage'),require('system')
local args=assert(args)
local t=system.millis()
-- Configuration has precedence over diagnostic status.
for _,name in ipairs({'config.json','ui-status.json'}) do
 local data=args.files[name]
 if data then
  local path=args.root..'/'..name;local ok,err=storage.write_file(path..'.tmp',data);assert(ok,err)
  local renamed,reason=storage.rename(path..'.tmp',path);assert(renamed,reason)
 end
end
print('Unveil background save_ms='..(system.millis()-t))
