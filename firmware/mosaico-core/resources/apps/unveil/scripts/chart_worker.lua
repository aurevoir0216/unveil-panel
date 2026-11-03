local capability,json,thread=require('capability'),require('json'),require('thread')
local a=assert(args)
local function progress(fraction)
 if a.prepare_job then thread.sync.queue_send(a.queue,json.encode({kind='chart_progress',prepare_job=a.prepare_job,fraction=fraction,endpoint=a.url}),500) end
end
local ok,result=xpcall(function()
 assert((a.job or ''):match('^[%w_-]*$'),'Invalid job')
 assert((a.capture or ''):match('^[%w_-]*$'),'Invalid capture')
 local path='/api/spectrogram?job='..(a.job or '')
 if a.capture and a.capture~='' then path=path..'&capture='..a.capture end
 local good,out,err=capability.call('http_request',{url=a.url:gsub('/+$','')..path,method='GET',timeout_ms=20000,max_body_bytes=65535},{max_output_bytes=73728})
 assert(good,tostring(out or err));assert(out:match('^HTTP 2%d%d[\r\n ]'),'Chart HTTP error')
 progress(1/3)
 local v=json.decode(assert(out:match('^[^\n]*\n(.*)$')))
 assert(v.width==96 and v.height==64 and #v.values==6144 and #v.activity==20 and #v.spectrum==96,'Invalid chart arrays')
 progress(2/3)
 local lvgl=require('lvgl');assert(lvgl.chart_publish,'Numeric chart handoff required')
 local activity=v.activity_precise or v.activity;local scale=v.activity_precise and (v.activity_scale or 100) or 10;local arange=v.activity_range_precise_db or v.activity_range_db
 local token=lvgl.chart_publish(v.values,activity,v.spectrum,math.floor(arange[1]*scale+.5),math.floor(arange[2]*scale+.5),math.floor(v.spectrum_range_db[1]*10+.5),math.floor(v.spectrum_range_db[2]*10+.5),a.generation or 0)
 return {kind='chart',prepare_job=a.prepare_job,generation=a.generation,chart_token=token,sample_rate=v.sample_rate,spectrum_min=v.spectrum_range_db[1],spectrum_max=v.spectrum_range_db[2],activity_min=arange[1],activity_max=arange[2],width=442,height=338,center_hz=v.center_hz,seconds=v.seconds,source_job=v.job,capture_id=v.capture_id,is_current=v.is_current,endpoint=a.url}
end,debug.traceback)
if not ok then result={kind='chart',prepare_job=a.prepare_job,generation=a.generation,error=tostring(result):sub(1,180),endpoint=a.url} end
thread.sync.queue_send(a.queue,json.encode(result),500)
