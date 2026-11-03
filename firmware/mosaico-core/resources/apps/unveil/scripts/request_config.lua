-- UI zero is a mode selector. Preserve the existing backend JSON contract.
local M={}
function M.start(a)
 local seconds=tonumber(a.analysis_seconds) or 2
 return {mode=seconds==0 and 'collect' or 'scan',capture_seconds=a.seconds,
  analysis_seconds=seconds==0 and 2 or seconds,discovery_min_rounds=a.discovery_min_rounds or 3}
end
return M
