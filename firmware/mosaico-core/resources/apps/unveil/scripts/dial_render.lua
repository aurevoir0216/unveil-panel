-- LVGL vector paths: no generated sprites or image buffers.
local lvgl=require('lvgl')
local M={}
function M.new(parent,dark)
 local self={levels={1,1,1},shown={}}
 self.surface=lvgl.object(parent,{x=65,y=65,w=350,h=350,bg_opa=0,border_width=0,pad=0,radius=0})
 self.surface:dial_init()
 function self:set(q,index) self.levels[q]=index end
 function self:draw()
  if self.shown[1]==self.levels[1] and self.shown[2]==self.levels[2] and self.shown[3]==self.levels[3] then return end
  self.surface:dial_values(self.levels[1],self.levels[2],self.levels[3])
  for q=1,3 do self.shown[q]=self.levels[q] end
 end
 return self
end
return M
