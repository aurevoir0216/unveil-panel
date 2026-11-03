-- System-owned motor: each pulse has a hardware timer, never a UI sleep.
local H={}
function H.new(call,clock,on_finished)
 local self={remaining=0,next_at=0,armed=false,seen_running=false}
 function self:pulse(ms)
  local ok,result=pcall(call,'system_control',{operation='vibrate',duration_ms=ms})
  if not ok or not result then self.remaining=0
  else print("Unveil haptic "..ms.."ms") end
 end
 function self:started(wait_for_view)
  self.remaining=0;self.armed=true;self.seen_running=false
  self.start_pending=not not wait_for_view
  if not self.start_pending then self:pulse(60) end
 end
 function self:start_presented()
  if self.start_pending then self.start_pending=false;self:pulse(60) end
 end
 function self:cancel_start() self.start_pending=false end
 function self:receive(state)
  if not self.armed then return end
  if not state.online then return end
  if state.running then self.seen_running=true;return end
  if state.state=='failed' then self.armed=false;self.start_pending=false;return end
  if state.state=='alert' or state.state=='safe' or state.state=='saved' then
   self.armed=false;print('Unveil feedback result='..state.state)
   if on_finished then on_finished(state.state) end
   self.remaining=state.state=='alert' and 3 or 1
   self.next_at=clock()
  end
 end
 function self:tick()
  local now=clock()
  if self.remaining>0 and now>=self.next_at then
   self.remaining=self.remaining-1;self.next_at=now+280;self:pulse(100)
  end
 end
 return self
end
return H
