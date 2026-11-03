-- One procedural native raster for the dial: persistent pixels and polar lookup.
local lvgl=require('lvgl')
local M={}
local function inert(o) o:clear_flag('CLICKABLE');o:clear_flag('SCROLLABLE');return o end
local function hex(s) return tonumber(s:sub(2),16) end
function M.new(parent,palette)
 local probe=inert(lvgl.image(parent,{x=24,y=24,w=432,h=432}))
 if not probe.radar_frame then
  probe:delete()
  return dofile(debug.getinfo(1,'S').source:match('^@(.*)/')..'/radar_legacy.lua').new(parent,palette)
 end
 probe:delete()
 local self={palette=palette,angle=0,sweeping=false,mode=false,fraction=nil}
 self.frame=inert(lvgl.object(parent,{x=0,y=0,w=480,h=416,pad=0,border_width=0,radius=0,bg_color=palette.bg}))
 self.image=inert(lvgl.image(self.frame,{x=24,y=24,w=432,h=432}))
 local axis=palette.dark and '#a5adb8' or '#727b86'
 local grid=palette.dark and '#687280' or '#8a919b'
 local function draw()
  local ms=self.image:radar_frame(math.floor(self.angle*4+.5),hex(self.mode and palette.orange or palette.bg),hex(grid),hex(palette.orange),self.sweeping and not self.paused,hex(palette.bg),hex(axis))
  self.draw_ms=ms
 end
 draw();print('Unveil radar cache_ms='..self.draw_ms)
 local function arc()
  return inert(lvgl.arc(self.frame,{x=49,y=49,w=382,h=382,bg_opa=0,pad=0,border_width=0}))
 end
 self.progress=arc();self.progress:add_flag('HIDDEN')
 function self:set_mode(alert)
  if self.mode==alert then return end
  self.mode=alert;draw()
 end
 function self:set_progress(fraction,running)
  fraction=math.max(0,math.min(1,fraction))
  if self.fraction==fraction then return end
  local increasing=self.fraction and fraction>self.fraction
  self.fraction=fraction
  if increasing and self.progress.set_sweep_motion then self.progress:set_sweep_motion(-205,math.floor(230*fraction+.5),3,hex(palette.orange),240)
  else self.progress:set_sweep(-205,math.floor(230*fraction+.5),3,hex(palette.orange)) end
  if fraction>0 then self.progress:clear_flag('HIDDEN') else self.progress:add_flag('HIDDEN') end
 end
 function self:set_sweeping(enabled)
  if self.sweeping==enabled then return end
  self.sweeping=enabled;self.last_ms=nil;draw()
 end
 function self:animate(now) end -- Native LVGL timer owns the sweep; Lua only changes state.
 function self:pause() self.paused=true;draw() end
 function self:resume() self.paused=false;draw() end
 return self
end
return M
