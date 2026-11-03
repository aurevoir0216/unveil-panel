-- Coalesce snapshots; NAND I/O belongs to a worker, never a touch callback.
local thread,system=require('thread'),require('system')
local M={}
function M.new(script,root)
 local self={pending={},due=0,poll_at=0,active=false,failures=0}
 function self:set(name,data,wait)
  self.pending[name]=data
  local deadline=system.millis()+(wait or 40)
  if self.due==0 or deadline<self.due then self.due=deadline end
 end
 function self:tick(now)
  if now<self.poll_at then return end;self.poll_at=now+40
  if self.active then
   local ok,info=thread.get(self.active_id)
   local status=ok and info:match('status=(%w+)')
   if status=='running' or status=='queued' then return end
   if status~='done' then
    for name,data in pairs(self.active) do if self.pending[name]==nil then self.pending[name]=data end end
    self.failures=self.failures+1;self.due=now+1000
    print('Unveil persistence retry='..self.failures)
   else self.failures=0 end
   self.active=false
  end
  if self.due==0 or now<self.due or not next(self.pending) then return end
  local batch=self.pending
  local ok,info=thread.start(script,{root=root,files=batch},{name='unveil_persist',exclusive='unveil_persist',replace=false,timeout_ms=10000,defer=true})
  if ok then self.active_id=info:match('job_id=(%w+)') or info:match('Started Lua job (%w+)');assert(self.active_id,'Missing persistence task id');self.pending={};self.active=batch;self.due=0 else self.due=now+1000 end
 end
 return self
end
return M
