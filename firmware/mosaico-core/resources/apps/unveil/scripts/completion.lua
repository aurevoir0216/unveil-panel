-- Completion feedback follows the rendered result, never a backend-only verdict.
local M={}
function M.new(clock,notify)
 local self={ready=nil,done=nil}
 function self:reset() self.ready=nil;self.done=nil end
 function self:rendered(job,eligible,visible,sequence)
  if not eligible or not visible then self.ready=nil;return end
  if self.done==job then return end
  if not self.ready or self.ready.job~=job then self.ready={job=job,at=clock(),sequence=sequence} end
 end
 function self:pending() return self.ready~=nil end
 function self:tick(job,eligible,visible,sequence,submitted,presented)
  local r=self.ready
  if not r then return end
  if not eligible or not visible or r.job~=job then self.ready=nil;return end
  -- Wait for the final 240 ms number/ring motion and at least one new render.
  if clock()-r.at>=300 and sequence>r.sequence then
   if submitted then
    r.frame=r.frame or submitted
    if presented<r.frame then return end
   end
   self.done=job;self.ready=nil
   print('Unveil result presented job='..tostring(job)..' percent=100 at='..clock())
   notify()
  end
 end
 return self
end
return M
