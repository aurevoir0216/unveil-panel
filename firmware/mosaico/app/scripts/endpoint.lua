local M={default=''}
function M.normalize(value,scheme)
 local s=value:match('^%s*(.-)%s*$'):gsub('/+$','')
 if not s:match('^https?://') then s=(scheme or 'https://')..s end
 local proto,hostport=s:match('^(https?://)([^/]+)$')
 if not proto or hostport:find('[%s@?#]') then return nil end
 local host,port=hostport:match('^([^:]+):(%d+)$')
 host=host or hostport
 if host:find(':',1,true) or #host>253 or not host:match('^[%w.-]+$') then return nil end
 if port and (tonumber(port)<1 or tonumber(port)>65535) then return nil end
 if host:match('^[%d.]+$') then
  local a,b,c,d=host:match('^(%d+)%.(%d+)%.(%d+)%.(%d+)$')
  if not a then return nil end
  for _,v in ipairs({a,b,c,d}) do if tonumber(v)>255 then return nil end end
 else
  if host:find('..',1,true) or host:sub(1,1)=='.' or host:sub(-1)=='.' then return nil end
  for label in host:gmatch('[^.]+') do
   if #label>63 or not label:match('^%w') or not label:match('%w$') then return nil end
  end
 end
 return proto..host:lower()..(port and ':'..port or '')
end
return M
