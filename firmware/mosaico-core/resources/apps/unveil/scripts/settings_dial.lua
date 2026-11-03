local lvgl,system=require('lvgl'),require('system')
local scripts=assert(debug.getinfo(1,'S').source:match('^@(.*)/[^/]+$'))
local Render=dofile(scripts..'/dial_render.lua')
local widths=dofile(scripts..'/dial_widths.lua')
local M={}
local function inert(o) o:clear_flag('CLICKABLE');o:clear_flag('SCROLLABLE');return o end
function M.new(parent,cfg,F,C,tr,save,localize_label)
 local self={pending=false,changed=0}
 local renderer=Render.new(parent,C.bg=='#000000')
 local adjusters={}
 function self:flush(now,force) if self.pending and (force or now-self.changed>1200) then save();self.pending=false end end
 local function changed() self.pending=true;self.changed=system.millis() end
 function self:prepare_motion(now) if now-self.changed>=200 then renderer:prepare_motion() end end
 function self:paging(active) renderer:paging(active) end
 local function label(s,x,y,font,color)
  local l=inert(lvgl.label(parent,{text=s,font=font,text_color=color}));if localize_label then localize_label(l,s) end;l:align('top_mid',x-240,y);return l
 end
 local params={
  {key='discovery_min_rounds',title=tr('轮数','Sweeps'),values={1,3,5},x=240,y=100,lx=240,ly=175,color='#e1dad5'},
  {key='capture_seconds',title=tr('采集','Capture'),values={.5,1,2,5,10},x=130,y=285,lx=185,ly=263,color='#dcd5e0'},
  {key='analysis_seconds',title=tr('分析','Analysis'),values={0,.5,1,2,5,10},x=330,y=285,lx=295,ly=263,color='#ded5e5',zero=true}}
 for q,p in ipairs(params) do
  local index=1
  for i,v in ipairs(p.values) do if math.abs(cfg[p.key]-v)<.001 then index=i end end
  local function fill()
   renderer:set(q,index)
  end
  fill()
  label(p.title,p.lx,p.ly,F.dialLabel,'#171619')
  local value=label(string.format('%g',p.values[index]),p.x,p.y,F.dial,C.orange)
  local unit=q~=1 and label('s',p.x,p.y+30,F.micro,C.orange) or nil
  local function position_unit()
   if not unit then return end
   local digits=string.format('%g',p.values[index]);local w=0
   for i=1,#digits do w=w+(widths[digits:sub(i,i)] or 0) end
   unit:align('top_mid',math.floor(p.x-240+w/2+7),p.y+30)
  end
  position_unit()
  local function set_index(n)
   if n==index then return end
   index=n;cfg[p.key]=p.values[n]
   cfg.mode=cfg.analysis_seconds==0 and 'collect' or 'scan'
   local function update()
   value:set_text(string.format('%g',p.values[n]));value:align('top_mid',p.x-240,p.y);position_unit();fill();renderer:draw()
   end
   if lvgl.batch then lvgl.batch(update) else update() end
   changed()
  end
  adjusters[q]=set_index

 end
 local input=lvgl.container(parent,{x=0,y=0,w=480,h=416,bg_opa=0,border_width=0,pad=0})
 input:radial_input(#params[1].values,#params[2].values,#params[3].values)
 input:on('value_changed',function() local v=input:radial_value();local q=math.floor(v/100);if adjusters[q] then adjusters[q](v%100) end end)
 input:on('released',function() self:flush(system.millis(),true) end)
 renderer:draw()
 return self
end
return M
