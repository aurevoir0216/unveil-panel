local storage,system=require('storage'),require('system')
local args=assert(args)
local t=system.millis()
local data=args.files['config.json']
if data then
 local path=args.root..'/config.json';local ok,err=storage.write_file(path..'.tmp',data);assert(ok,err)
 local renamed,reason=storage.rename(path..'.tmp',path);assert(renamed,reason)
end
print('Unveil background save_ms='..(system.millis()-t))
