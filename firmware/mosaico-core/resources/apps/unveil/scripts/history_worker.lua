local capability,json,thread=require('capability'),require('json'),require('thread')
local a=assert(args)
local function send(v) v.kind='history';v.generation=a.generation;v.endpoint=a.url;assert(thread.sync.queue_send(a.queue,json.encode(v),500)) end
local ok,err=xpcall(function()
 assert((a.revision or ''):match('^[%w]*$'),'Invalid revision')
 local good,out,error=capability.call('http_request',{url=a.url:gsub('/+$','')..'/api/device/history?revision='..(a.revision or ''),method='GET',timeout_ms=20000,max_body_bytes=65535},{max_output_bytes=73728})
 assert(good,tostring(out or error));assert(out:match('^HTTP 2%d%d[\r\n ]'),'History HTTP error')
 local value=json.decode(assert(out:match('^[^\n]*\n(.*)$')))
 assert(type(value.records)=='table' and type(value.revision)=='string','Invalid history response')
 local batch={}
 for _,record in ipairs(value.records) do
  batch[#batch+1]=record
  if #json.encode(batch)>1400 then
   local last=table.remove(batch);if #batch>0 then send({records=batch}) end;batch={last}
  end
 end
 if #batch>0 then send({records=batch}) end
 send({done=true,unchanged=value.unchanged,revision=value.revision,count=value.count})
end,debug.traceback)
if not ok then send({done=true,error=tostring(err):sub(1,140)}) end
