local M={}
function M.derive(s,online)
 if not online then return 'offline' end
 if not s or s.status=='idle' then return 'ready' end
 if s.status=='running' then return s.stage=='analyzing' and 'analyzing' or s.stage=='capturing' and 'capturing' or 'scanning' end
 if s.status=='failed' then return 'failed' end
 if s.status~='completed' then return 'incomplete' end
 if (s.config or {}).mode=='collect' then return 'saved' end
 local outputs={}
 for _,r in ipairs(s.results or {}) do
  if r.status=='analyzed' then
   outputs[#outputs+1]=r
   if (r.prediction or r.raw_prediction)=='CAMERA' and r.classification_validity~=false then return 'alert' end
  end
 end
 local n=(s.candidate_processing or {}).selected_windows
 if type(n)~='number' or n<=0 or n~=math.floor(n) then return 'incomplete' end
 local records=s.records or {}
 if #records~=n or #outputs~=n then return 'incomplete' end
 local ids,seen={},{}
 for _,r in ipairs(records) do
  if type(r.capture_id)~='string' or r.capture_id=='' or ids[r.capture_id] then return 'incomplete' end
  ids[r.capture_id]=true
 end
 for _,r in ipairs(outputs) do
  if not ids[r.capture_id] or seen[r.capture_id] or (r.prediction or r.raw_prediction)~='NON_CAMERA' or r.classification_validity==false then return 'incomplete' end
  seen[r.capture_id]=true
 end
 if type((s.sweep_coverage or {}).fraction)=='number' and s.sweep_coverage.fraction<1 then return 'incomplete' end
 return 'safe'
end
function M.progress(s)
 if s.status=='completed' then return 100 end
 if s.status~='running' then return nil end
 local function ratio(a,b) if type(a)=='number' and type(b)=='number' and b>0 then return math.max(0,math.min(100,math.floor(a/b*100+.5))) end end
 local p=s.candidate_processing or {}
 if s.stage=='capturing' then local c=s.capture_progress or {};return ratio(c.completed_samples,c.total_samples) or ratio(p.captured,p.selected_windows) end
 if s.stage=='analyzing' then local c=s.stft_progress or {};return ratio(c.completed_tiles,c.total_tiles) or ratio(p.analyzed,p.selected_windows) end
 return ratio((s.sweep_coverage or {}).fraction,1)
end
function M.total_progress(s)
 if s.status=='completed' then return 100 end
 if s.status~='running' then return nil end
 if type(s.total_progress)=='number' then return math.max(0,math.min(100,s.total_progress)) end
 local p=s.candidate_processing or {};local n=p.selected_windows
 if type(n)~='number' or n<=0 then return nil end
 local captured=math.min(n,p.captured or 0)
 local analysis=(s.config or {}).mode~='collect'
 local completed=captured+(analysis and math.min(n,p.analyzed or 0) or 0)
 return math.floor(completed/(n*(analysis and 2 or 1))*100+.5)
end
return M
