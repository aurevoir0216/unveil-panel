local lvgl,system=require('lvgl'),require('system')
local t=system.millis();local ready=lvgl.font_prepare(assert(args).path);print('Unveil Wi-Fi font prepared='..tostring(ready)..' read_ms='..(system.millis()-t))
