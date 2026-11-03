-- One bounded network operation. No UI ownership and no endless child task.
local capability,json,thread=require('capability'),require('json'),require('thread')
local a=assert(args)
local system=require('system')
local scripts=assert(debug.getinfo(1,'S').source:match('^@(.*)/[^/]+$'))
package.path=scripts..'/?.lua;'..package.path
local function diagnostic(value) pcall(system.publish_status,'connection-status.json',json.encode(value)) end
local policy=require('state')
local request_config=require('request_config')
local function request(path,body)
 local req={url=a.url:gsub('/+$','')..path,method=body and 'POST' or 'GET',timeout_ms=15000,max_body_bytes=60000}
 if body then req.body=json.encode(body);req.headers={['Content-Type']='application/json'} end

 local ok,out,err=capability.call('http_request',req)

 assert(ok,tostring(out or 'HTTP failed')..' '..tostring(err or ''))
 assert(type(out)=='string' and out:match('^HTTP 2%d%d[\r\n ]') and not (out:match('^[^\n]*') or ''):find('truncated',1,true),type(out)=='string' and (out:match('^[^\r\n]*') or 'HTTP response unavailable') or 'HTTP response unavailable')
 return json.decode(assert(out:match('^[^\n]*\n(.*)$'),'Missing JSON body'))
end
local ok,result=xpcall(function()
 local notice=''
 local start_ack=false
 local command_error=''
 local previous_job=a.previous_job or ''
 if a.command=='start' and previous_job=='' then local good,old=pcall(request,'/api/device/status');if good then previous_job=old.job_id or '' end end
 if a.command then
  local accepted,command_result=pcall(request,a.command=='start' and '/api/device/start' or '/api/device/cancel',a.command=='start' and request_config.start(a) or {})
  start_ack=a.command=='start' and accepted
  if not accepted then command_error=tostring(command_result):sub(1,140);notice=command_error end
 end
 local payload=request('/api/device/status')
 local s=payload;assert(type(s)=='table' and type(s.status)=='string','Invalid status')
 if a.command=='start' and not start_ack then
  start_ack=type(s.job_id)=='string' and s.job_id~='' and s.job_id~=previous_job
  if not start_ack then return {online=true,state='failed',running=false,percent=0,captured=0,analyzed=0,selected=0,job='',server_job=s.job_id or '',notice=command_error,start_failed=true} end
 end
 if not a.session_started and not start_ack then
  return {online=true,server_job=s.job_id or '',test=not not s.test,state='ready',running=false,percent=0,captured=0,analyzed=0,selected=0,job='',notice=notice}
 end
 local p=s.candidate_processing or {}
 local function percent(progress)
  if type(progress)~='table' then return nil end
  local done=progress.completed_samples or progress.completed_units
  local total=progress.total_samples or progress.total_units
  if type(done)=='number' and type(total)=='number' and total>0 then return math.max(0,math.min(100,100*done/total)) end
 end
 return {online=true,start_ack=start_ack,display_time=s.display_time,timestamp=s.finished_at or s.started_at,test=not not s.test,chart_job=s.source_job,chart_capture=s.records and (p.captured or 0)>0 and s.records[math.min(#s.records,p.captured)] and s.records[math.min(#s.records,p.captured)].capture_id,mode=(s.config and s.config.mode) or (a.analysis_seconds==0 and 'collect' or 'scan'),state=policy.derive(s,true),running=s.status=='running',stage=s.stage or '',job=s.job_id or '',percent=policy.total_progress(s),capture_percent=percent(s.capture_progress),analysis_percent=percent(s.analysis_progress),captured=p.captured or 0,analyzed=p.analyzed or 0,selected=p.selected_windows or 0,notice=notice~='' and notice or s.error or ''}
end,debug.traceback)
if not ok then result={online=false,state='offline',notice=tostring(result):sub(1,140)} end

result.request_generation=a.request_generation
result.endpoint=a.url
result.command_complete=a.command or ''
local queued,queue_error=thread.sync.queue_send(a.queue,json.encode(result),500)
diagnostic({stage='complete',online=result.online,notice=result.notice,url=a.url,queued=not not queued,queue_error=queue_error,job=result.job,captured=result.captured,analyzed=result.analyzed,selected=result.selected})
