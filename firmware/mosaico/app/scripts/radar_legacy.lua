-- Native LVGL radar: no raster assets, image rotation or per-frame bitmap upload.
local lvgl=require('lvgl')
local M={}
local CX,CY,R=240,240,163
local function rounded(v) return math.floor(v+.5) end
local function point(r,a) return {x=rounded(CX+r*math.cos(a)),y=rounded(CY+r*math.sin(a))} end
local function inert(o) o:clear_flag('CLICKABLE');o:clear_flag('SCROLLABLE');return o end
local function blend(fg,bg,t)
 local f=tonumber(fg:sub(2),16);local b=tonumber(bg:sub(2),16)
 local out=0
 for _,shift in ipairs({16,8,0}) do local fc=(f>>shift)&255;local bc=(b>>shift)&255;out=(out<<8)|rounded(bc+(fc-bc)*t) end
 return string.format('#%06x',out)
end
local xs,ys={},{}
for i=0,359 do local a=math.rad(i-90);xs[i]=rounded(R*math.cos(a));ys[i]=rounded(R*math.sin(a)) end
function M.new(parent,palette)
 local self={palette=palette,grids={},ticks={},rays={},fan={},sweeping=nil,mode=nil,fraction=nil}
 local frame=inert(lvgl.object(parent,{x=0,y=0,w=480,h=416,pad=0,border_width=0,radius=0,bg_color=palette.bg}))
 self.frame=frame
 local axis=palette.dark and '#a5adb8' or '#727b86'
 local grid=palette.dark and '#687280' or '#8a919b'
 self.grid_color=grid
 self.disc=inert(lvgl.object(frame,{x=76,y=76,w=328,h=328,radius=164,pad=0,border_width=0,bg_color=palette.bg}))
 local function circle(r,color)
  return inert(lvgl.object(frame,{x=CX-r,y=CY-r,w=2*r,h=2*r,radius=r,pad=0,bg_opa=0,border_width=2,border_color=color}))
 end
 local function line(points,color,width)
  return inert(lvgl.line(frame,{points=points,line_color=color,line_width=width or 1}))
 end
 -- Filled 72-degree trail from native polyline strips, beneath the grid.
 -- Opaque preblended colors avoid repeatedly blending overlapping radial strokes.
 -- Each strip owns a fixed point buffer; no bitmap or allocation per frame.
 local probe=lvgl.arc(frame,{x=CX-R,y=CY-R,w=2*R,h=2*R,bg_opa=0,border_width=0,pad=0})
 probe:clear_flag('CLICKABLE');probe:add_flag('HIDDEN')
 self.native_fan=probe.set_sweep~=nil
 if self.native_fan then
  for band=0,3 do
   local o=band==0 and probe or inert(lvgl.arc(frame,{x=CX-R,y=CY-R,w=2*R,h=2*R,bg_opa=0,border_width=0,pad=0}))
   o:add_flag('HIDDEN')
   self.fan[#self.fan+1]={obj=o,sweep=72-band*18,color=tonumber(blend(palette.orange,palette.bg,.045+band*.045):sub(2),16)}
  end
 else
  probe:delete()
  for band=0,2 do
   local pts={};for j=0,4 do pts[#pts+1]={x=CX,y=CY};pts[#pts+1]={x=CX,y=CY-R} end
   local o=line(pts,blend(palette.orange,palette.bg,.035+.16*(2-band)/2),14)
   o:add_flag('HIDDEN');self.fan[#self.fan+1]={obj=o,points=pts,lag=band*24}
  end
 end
 for _,r in ipairs({41,82,124,163}) do self.grids[#self.grids+1]={obj=circle(r,grid),border=true} end
 for i=0,3 do local a=i*math.pi/4;self.grids[#self.grids+1]={obj=line({point(R,a),point(R,a+math.pi)},grid,2)} end
 circle(174,axis)
 local arc={}
 for i=0,100 do arc[#arc+1]=point(191,math.rad(-205+2.3*i)) end
 if self.native_fan then
  local track=inert(lvgl.arc(frame,{x=CX-191,y=CY-191,w=382,h=382,bg_opa=0,pad=0,border_width=0}))
  track:set_sweep(-205,230,1,tonumber(axis:sub(2),16))
 else line(arc,axis) end
 local tick_points={}
 for i=0,100,5 do
  local a=math.rad(-205+2.3*i);local length=i%25==0 and 10 or i%5==0 and 6 or 3
  tick_points[#tick_points+1]=point(198,a);tick_points[#tick_points+1]=point(198+length,a);tick_points[#tick_points+1]=point(198,a)
 end
 self.tick_ring=line(tick_points,axis,1)
 self.axis=axis;self.progress_points={}
 for i=0,100 do self.progress_points[i+1]=point(191,math.rad(-205)) end
 if self.native_fan then self.progress=inert(lvgl.arc(frame,{x=CX-191,y=CY-191,w=382,h=382,bg_opa=0,pad=0,border_width=0}))
 else self.progress=line(self.progress_points,palette.orange,3) end
 self.progress:add_flag('HIDDEN')
 -- Faint trails first; the leading line is drawn last. Fixed two-point buffers.
 for i,style in ipairs({{lag=0,alpha=.9,width=2}}) do
  local pts={{x=CX,y=CY-28},{x=CX,y=CY-R}}
  local ray=line(pts,blend(palette.orange,palette.bg,style.alpha),style.width);ray:add_flag('HIDDEN')
  self.rays[i]={obj=ray,points=pts,lag=style.lag,last=nil}
 end
 function self:set_mode(alert)
  if self.mode==alert then return end
  self.mode=alert;self.disc:set_style({bg_color=alert and self.palette.orange or self.palette.bg})
  local color=self.grid_color
  for _,g in ipairs(self.grids) do g.obj:set_style(g.border and {border_color=color} or {line_color=color}) end
 end
 function self:set_progress(fraction,running,analysis)
  fraction=math.max(0,math.min(1,fraction))
  if self.fraction==fraction and self.running==running then return end
  self.fraction=fraction;self.running=running
  if self.native_fan then
   self.progress:set_sweep(-205,math.floor(230*fraction+.5),3,tonumber(self.palette.orange:sub(2),16))
  else
  for i=0,100 do
   local a=math.rad(-205+230*fraction*i/100);local p=self.progress_points[i+1]
   p.x=rounded(CX+191*math.cos(a));p.y=rounded(CY+191*math.sin(a))
  end
  self.progress:set_points(self.progress_points);self.progress:set_style({line_color=self.palette.orange})
  end
  if fraction>0 then self.progress:clear_flag('HIDDEN') else self.progress:add_flag('HIDDEN') end
 end
 function self:set_sweeping(enabled)
  if self.sweeping==enabled then return end
  self.sweeping=enabled;self.last_ms=nil
  for _,strip in ipairs(self.fan) do if enabled then strip.obj:clear_flag('HIDDEN') else strip.obj:add_flag('HIDDEN') end end
  for _,ray in ipairs(self.rays) do if enabled then ray.obj:clear_flag('HIDDEN') else ray.obj:add_flag('HIDDEN') end end
 end
 local function animate(self,now)
  if not self.sweeping then return end
  local elapsed=self.last_ms and math.max(0,math.min(70,now-self.last_ms)) or 16
  self.last_ms=now;self.angle=((self.angle or 0)+elapsed*.06)%360
  local degrees=math.floor(self.angle)
  if self.last_degrees==degrees then return end
  self.last_degrees=degrees
  for _,strip in ipairs(self.fan) do
   if self.native_fan then
    strip.obj:set_sweep(degrees-strip.sweep-90,strip.sweep,R,strip.color)
   else
   for j=0,4 do
    local a=(degrees-strip.lag-j*5)%360;local p=strip.points[2*j+2]
    p.x=CX+xs[a];p.y=CY+ys[a]
   end
   strip.obj:set_points(strip.points)
   end
  end
  for _,ray in ipairs(self.rays) do
   local a=(degrees-ray.lag)%360
   if a~=ray.last then
    ray.last=a;local p=ray.points
    p[1].x=CX+rounded(xs[a]*28/R);p[1].y=CY+rounded(ys[a]*28/R)
    p[2].x=CX+xs[a];p[2].y=CY+ys[a]
    ray.obj:set_points(p)
   end
  end
 end
 function self:animate(now)
  if self.native_fan and lvgl.batch then lvgl.batch(animate,self,now) else animate(self,now) end
 end
 self:set_mode(false);self:set_progress(0,false);self:set_sweeping(false)
 return self
end
return M
