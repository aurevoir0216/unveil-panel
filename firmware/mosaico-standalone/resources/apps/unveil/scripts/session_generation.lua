-- Keep worker generations exactly representable by embedded Lua float32 JSON.
local M={}
function M.seed(milliseconds)
 return (milliseconds%120000)*64
end
return M
