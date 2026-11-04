-- Reuses the device's Mosaico Wi-Fi manager and configuration store.
local lvgl,json,system=require('lvgl'),require('json'),require('system')
local M={}
local thread=require('thread')
local scripts=assert(debug.getinfo(1,'S').source:match('^@(.*)/[^/]+$'))
local font_path='apps/unveil/assets/Unveil-Wifi.otf'
local shared_font,next_prepare=nil,0
function M.prepare()
 if shared_font or not lvgl.font_prepare or system.millis()<next_prepare then return end
 next_prepare=system.millis()+1500
 if lvgl.font_ready(font_path) then return end
 local ok,info=thread.get('unveil_wifi_font');local status=ok and info:match('status=(%w+)')
 if status~='running' and status~='queued' then thread.start(scripts..'/font_worker.lua',{path=font_path},{name='unveil_wifi_font',exclusive='unveil_wifi_font',timeout_ms=30000,defer=true}) end
end
local function acquire_font()
 if not shared_font and lvgl.font_ready and lvgl.font_ready(font_path) then
  local ok,font=pcall(lvgl.font_load,font_path,{size=24,cache_size=128,prepared=true})
  if ok then shared_font=font;print('Unveil Wi-Fi font active='..tostring(font:is_valid())) end
 end
 return shared_font
end
local function short(s)
 local ok,result=pcall(function()
  local result,units={},0
  for _,c in utf8.codes(s) do
   local ch=utf8.char(c)
   local n=c>=128 and 24 or ch:match('[MWmw@#]') and 24 or ch:match('[A-Z]') and 17 or 14
   if units+n>304 then return table.concat(result)..'...' end
   result[#result+1]=utf8.char(c);units=units+n
  end
  return table.concat(result)
 end)
 return ok and result or s:gsub('[^%g ]','?'):sub(1,24)
end
function M.new(parent,F,C,tr)
 local self={stage='list',next_poll=0,status={},pending_scan=true}
 self.font=acquire_font() or F.small;M.prepare()
 local function inert(o) o:clear_flag('CLICKABLE');o:clear_flag('SCROLLABLE');return o end
 local function box(p,x,y,w,h,color,radius)
  return inert(lvgl.object(p,{x=x,y=y,w=w,h=h,pad=0,border_width=0,radius=radius or 0,bg_color=color or C.bg}))
 end
 local function text(p,s,x,y,w,h,f,color)
  return inert(lvgl.label(p,{text=s,x=x,y=y,w=w,h=h,font=f or F.status,text_color=color or C.fg}))
 end
 local function button(p,s,x,y,w,h,fn,active)
  local b=box(p,x,y,w,h,active and C.orange or C.card,8);b:add_flag('CLICKABLE')
  if lvgl.guard_tap then lvgl.guard_tap(b,18) end
  local l=inert(lvgl.label(b,{text=s,font=F.status,text_color=active and '#171619' or C.fg}));l:align('center',0,0)
  b:on('clicked',fn)
  local previous=s
  return {set=function(v) if previous==v then return end;previous=v;l:set_text(v);l:align('center',0,0) end}
 end
 local content=box(parent,0,86,480,344,C.bg)
 function self:call(op,a,b)
  if not lvgl.wifi then return nil end
  local good,data=pcall(lvgl.wifi,op,a,b)
  if not good then self.message=tr('连接失败','Failed');return nil end
  local v=json.decode(data);self.status=v
  if not v.ok then self.message=v.error end
  return v
 end
 local render_list,render_password,update_list
 local function join(ap,password)
  local v=self:call('connect',ap.ssid,password)
  if v and v.ok then
   self.message=nil;self.stage='list';render_list()
  elseif self.error then self.error:set_text(tr('密码无效','Invalid password')) end
 end
 render_password=function(ap)
  self.stage='password';self.target=ap;content:clean()
  text(content,short(ap.ssid),24,0,432,42,self.font)
  local input=lvgl.textarea(content,{x=24,y=48,w=432,h=44,one_line=true,password=true,max_length=64,placeholder=tr('密码','Password'),font=F.small,bg_color=C.card,text_color=C.fg,border_width=1,border_color=C.line,pad=5,radius=8})
  local keyboard=lvgl.keyboard(content,{align='top_left',x=12,y=100,w=456,h=184,textarea=input,popovers=false})
  if keyboard.set_palette then keyboard:set_palette(tonumber(C.bg:sub(2),16),tonumber(C.card:sub(2),16),tonumber(C.fg:sub(2),16),tonumber(C.orange:sub(2),16)) end
  self.error=text(content,'',24,286,432,18,F.micro,C.orange)
  local function submit()
   local password=input.get_text and input:get_text() or ''
   if password=='' and self.status.ssid~=ap.ssid then self.error:set_text(tr('输入密码','Password'));return end
   join(ap,password~='' and password or nil)
  end
  button(content,tr('连接','JOIN'),150,304,180,36,submit,true)
  input:on('ready',submit)
  keyboard:on('cancel',render_list)
  input:on('value_changed',function() if self.error and self.error:is_valid() then self.error:set_text('') end end)
 end
 update_list=function()
  if self.stage~='list' then return end
  local s=self.status
  self.toggle.set(s.enabled and 'Wi-Fi ON' or 'Wi-Fi OFF')
  self.rescan.set(s.scanning and '...' or tr('扫描','SCAN'))
  local ssid=not shared_font and (s.ssid or ''):find('[\128-\255]') and '...' or short(s.ssid or '');if self.last_ssid~=ssid then self.ssid:set_text(ssid);self.last_ssid=ssid end
  local status=s.connected and s.ip or ({connecting=tr('连接中','Connecting'),auth_failed=tr('密码错误','Wrong password'),ap_not_found=tr('未找到网络','Not found'),scanning=tr('扫描中','Scanning'),disabled='OFF',retry_wait=tr('重试中','Retrying')})[tostring(s.state):lower()] or ''
  if self.last_info~=status then self.info:set_text(status);self.last_info=status end
  local signature=tostring(s.revision)..tostring(s.enabled)..tostring(s.scanning)
  if signature==self.list_signature then return end
  self.list_signature=signature;self.list:clean()
  if not s.enabled then return end
  if #(s.aps or {})==0 then text(self.list,s.scanning and '...' or '--',24,32,432,36,F.small,C.muted);return end
  for i,ap in ipairs(s.aps) do
   local row=box(self.list,24,(i-1)*56,432,54,C.bg);row:add_flag('CLICKABLE')
   if lvgl.guard_tap then lvgl.guard_tap(row,18) end
   local readable=shared_font or not ap.ssid:find('[\128-\255]')
   text(row,readable and short(ap.ssid) or '...',0,6,348,44,self.font)
   local signal=text(row,tostring(ap.rssi),360,14,70,30,F.status,C.muted)
   row:on('clicked',function()
    if not readable then return end
    if ap.secured then render_password(ap) else join(ap,'') end
   end)
  end
 end
 render_list=function()
  content:clean();self.stage='list';self.list_signature=nil;self.error=nil;self.last_ssid=nil;self.last_info=nil
  self.toggle=button(content,'Wi-Fi',24,0,180,40,function()
   local enable=not self.status.enabled;self.pending_scan=enable;self:call('enable',enable);self.next_poll=0
  end)
  self.rescan=button(content,tr('扫描','SCAN'),300,0,156,40,function()
   if self.status.enabled then self:call('scan') else self.pending_scan=true;self:call('enable',true) end
   self.next_poll=0
  end)
  self.ssid=text(content,'',24,48,432,32,self.font,C.fg)
  self.info=text(content,'',24,80,432,22,F.status,C.muted)
  self.list=lvgl.object(content,{x=0,y=108,w=468,h=234,pad=0,border_width=0,radius=0,bg_color=C.bg})
  self.list:add_flag('SCROLLABLE');self.list:set_scroll({dir='ver',scrollbar='auto'})
  update_list()
 end
 function self:tick(now)
  if now<self.next_poll then return end
  self.next_poll=now+1000
  if not shared_font then M.prepare() end
  local font=acquire_font();if font and self.font~=font and self.stage=='list' then self.font=font;render_list() end
  self:call('status')
  if self.pending_scan and self.status.enabled then self.pending_scan=false;self:call('scan') end
  if self.stage=='list' then
   if lvgl.batch then lvgl.batch(update_list) else update_list() end
  end
 end
 function self:back()
  if self.stage=='password' then render_list();return true end
  return false
 end
 function self:close()
  self.font=nil
  if shared_font then pcall(function() shared_font:delete() end);shared_font=nil end
  if lvgl.font_prepare then pcall(lvgl.font_prepare,false) end
 end
 self:call('status');render_list();self:tick(system.millis())
 return self
end
return M
