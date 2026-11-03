-- Desktop application loading page; the system boot keeps Mosaico branding.
local lvgl,delay,system=require('lvgl'),require('delay'),require('system')
lvgl.init({hold_first_frame=true,unveil_input=true})
local screen=lvgl.create_screen();screen:set_style({bg_color='#000000',pad=0})
local font=lvgl.font_load('apps/unveil/assets/Doto-Regular.ttf',{size=96,cache_size=16})
local word=lvgl.label(screen,{text='Unveil',font=font,text_color='#f54b2c'})
word:align('center',0,0);word:clear_flag('CLICKABLE');screen:load()
local before=lvgl.present_status();lvgl.present_ready()
local deadline=system.millis()+350
repeat
 delay.delay_ms(5)
 local frame=lvgl.present_status()
 if frame.ok and frame.transferred>before.transferred then
  print('Unveil app loading frame transferred='..frame.transferred);break
 end
until system.millis()>=deadline
_G.unveil_loader={screen=screen,font=font}
local scripts=assert(debug.getinfo(1,'S').source:match('^@(.*)/[^/]+$'))
local ok,reason=xpcall(function() dofile(scripts..'/unveil.lua') end,debug.traceback)
if not ok then pcall(lvgl.deinit);error(reason) end
