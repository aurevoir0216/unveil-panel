-- Visual feedback only. Capture requests and server progress stay independent.
local M={}
function M.new(haptic) return {haptic=haptic~=false} end
function M.step(f,settled,p)
 if not settled or not p.ok then f.generation=nil;f.sequence=nil;f.frame=nil;return end
 if f.generation~=p.generation then
  f.generation=p.generation;f.sequence=p.sequence;f.frame=nil
  return 'paint'
 end
 if not f.frame and p.sequence>f.sequence then f.frame=p.submitted end
 if f.frame and p.transferred>=f.frame and p.presented>=f.frame then return 'start' end
end
return M
