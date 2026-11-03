-- Unveil: 480 px / ~40 mm. Native tileview gestures; shell owns bottom-edge exit.
local lvgl,storage,json,thread,delay=require('lvgl'),require('storage'),require('json'),require('thread'),require('delay')
local system=require('system')
local capability=require('capability')
local scripts=assert(debug.getinfo(1,'S').source:match('^@(.*)/[^/]+$'))
local app=scripts:match('^(.*)/scripts$')
local Radar=dofile(scripts..'/radar.lua')
local WiFi=dofile(scripts..'/wifi_ui.lua')
local wifi_page
local radar
local chart_data
local session_generation=dofile(scripts..'/session_generation.lua').seed(system.millis())
local chart_epoch=session_generation
local chart_prepare_job,chart_prepare_progress,chart_prepare_failed,chart_retry_at=nil,0,false,0
local server_test=false
local session_started=false
local run_generation=session_generation
local built_language,last_signature
local translations,localized_labels={},{}
local request_chart,paint_details
local chart_view=1
local details_signature
local history_ui
local history_signature,history_parent
local history_dirty=true
local paint_history
local paging=false
local play_sound
local haptics=dofile(scripts..'/haptics.lua').new(capability.call,system.millis,function(kind) if play_sound then play_sound(kind) end end)
local settings_controller
local system_values={}
local details_ui
local SettingsDial=dofile(scripts..'/settings_dial.lua')
local capture_metric,analysis_metric,metric_labels,metric_boxes
local Endpoint=dofile(scripts..'/endpoint.lua')
local cfg={pi_url=Endpoint.default,dark=false,language='zh',capture_seconds=2,analysis_seconds=2,discovery_min_rounds=3,mode='scan',replay_job='',history_newest=true,sound_muted=false,sound_volume=40}
local ok,saved=pcall(function() return json.decode(storage.read_file(app..'/config.json')) end)
if ok and type(saved)=='table' then for k in pairs(cfg) do if type(saved[k])==type(cfg[k]) then cfg[k]=saved[k] end end end
if cfg.mode=='collect' then cfg.analysis_seconds=0 end
cfg.mode=cfg.analysis_seconds==0 and 'collect' or 'scan'
cfg.pi_url=Endpoint.normalize(cfg.pi_url) or Endpoint.default
cfg.sound_volume=math.floor(math.max(0,math.min(100,cfg.sound_volume)))
play_sound=function(kind)
 if lvgl.sound then pcall(lvgl.sound,kind=='alert' and 'alarm' or 'safe',cfg.sound_volume,cfg.sound_muted) end
end
local queue,job='unveil_response','unveil_http'
local splash_screen,splash_font
local connection_label,queued_command
local state,history={online=false,state='ready',running=false,percent=0},{}
local completion=dofile(scripts..'/completion.lua').new(system.millis,function() haptics:receive(state) end)
local history_revision,history_generation,history_buffer='',session_generation,{}
local history_loading,history_next_poll=false,0
local history_loaded,history_error,history_deadline=false,nil,0
local request_history
local function load_history()
 history={};history_revision='';history_signature=nil;history_dirty=true;history_next_poll=0;history_loaded=false;history_error=nil;history_loading=false;history_deadline=0
end
local page,sub,dirty,owned,ticks,pending,angle=1,nil,true,false,0,false,0
local screen,pager,home,readout,percent_label,phase,progress,sweep,action,details_counts,details_analysis,details_state
local shown_main,shown_phase,shown_capture,shown_analysis
local F,C,painted_alert,editor,editor_scheme,edit_value,edit_scheme
local primary_pager,primary_tiles,overlay,overlay_pager,nav_marker,nav_x,nav_from,nav_to,nav_started
local built_dark,home_bg
local brand_bar,brand_screen,brand_label,building_primary
local nav_items,nav_selected={},nil
local function select_indicator(index,immediate)
 index=math.max(1,math.min(5,index))
 if nav_selected==index then return end
 nav_selected=index
 print('Unveil page='..index)
 for i,item in ipairs(nav_items) do
  if i==index then item.active:clear_flag('HIDDEN');item.dot:add_flag('HIDDEN')
  else item.active:add_flag('HIDDEN');item.dot:clear_flag('HIDDEN') end
 end
end

local function tr(c,e)
 local pair={c,e};translations[c]=pair;translations[e]=pair
 return cfg.language=='zh' and c or e
end
local function localize_label(o,value)
 local pair=translations[value]
 if pair then localized_labels[#localized_labels+1]={o,pair[1],pair[2]} end
 return o
end
local function update_language()
 local started=system.millis()
 local live={}
 for _,item in ipairs(localized_labels) do
  if item[1]:is_valid() then item[1]:set_text(cfg.language=='zh' and item[2] or item[3]);live[#live+1]=item end
 end
 localized_labels=live;built_language=cfg.language;last_signature=nil;history_signature=nil;history_dirty=true
 print('Unveil language updated in place: '..cfg.language..' labels='..#live..' update_ms='..(system.millis()-started))
end
local persistence=dofile(scripts..'/persistence.lua').new(scripts..'/persist_worker.lua',app)
local function save() cfg.mode=cfg.analysis_seconds==0 and 'collect' or 'scan';persistence:set('config.json',json.encode(cfg),40) end
local last_health
local function health(v)
 local data=json.encode(v);if data~=last_health then pcall(system.publish_status,'ui-status.json',data);last_health=data end
end
local function active()
 local good,info=thread.get(job)
 local s=good and info:match('status=(%w+)')
 return s=='running' or s=='queued'
end
local scan_start_pending=false
local scan_requested=false
local scan_previous_job=''
local start_feedback
local scan_visual_wait=false
local ScanGate=dofile(scripts..'/scan_gate.lua')
local function start(command)
 if command=='start' and not scan_start_pending then scan_visual_wait=true;haptics:started(true);start_feedback=ScanGate.new(true);if radar then radar:set_sweeping(false) end;scan_previous_job=state.server_job or state.job or '';scan_requested=true;scan_start_pending=true; completion:reset();run_generation=run_generation+1;session_started=false;chart_prepare_job=nil;chart_prepare_progress=0;chart_prepare_failed=false;state={online=state.online,test=state.test,state='ready',running=false,percent=0,captured=0,analyzed=0,selected=0,job=''};dirty=true;last_signature=nil;chart_epoch=chart_epoch+1;chart_data=nil;details_signature=nil;if lvgl.chart_clear then lvgl.chart_clear() end;if primary_tiles and paint_details then paint_details(primary_tiles[3]) end end
 if pending or active() then if command then queued_command=command end;return end
 if cfg.pi_url=='' then if command then scan_start_pending=false;scan_visual_wait=false;start_feedback=nil end;return end
 if command then queued_command=nil end
 local started,err=thread.start(scripts..'/worker.lua',{url=cfg.pi_url,replay_job='',queue=queue,command=command,previous_job=scan_previous_job,request_generation=run_generation,session_started=session_started,seconds=cfg.capture_seconds,analysis_seconds=cfg.analysis_seconds,discovery_min_rounds=cfg.discovery_min_rounds,mode=cfg.analysis_seconds==0 and 'collect' or 'scan'},{name=job,exclusive=job,replace=false,timeout_ms=40000,defer=true})
 pending=not not started
 if not started then scan_start_pending=false;scan_visual_wait=false;start_feedback=nil;haptics:cancel_start();state={online=false,state='offline'};print(tostring(err)) end
end
local function request_scan()
 if state.running or scan_start_pending then return false end
 start('start');return true
end
request_history=function()
 local now=system.millis()
 if cfg.pi_url=='' then history_next_poll=now+5000;return end
 -- A completed producer can still have queued batches. Do not replace its
 -- generation or partial buffer until the UI consumes the final message.
 if history_loading then
  if now<history_deadline then return end
  history_loading=false;history_error='timeout';history_dirty=true;history_buffer={}
 end
 local ok,info=thread.get('unveil_history');local status=ok and info:match('status=(%w+)')
 if status=='running' or status=='queued' then return end
 history_generation=history_generation+1;history_buffer={};history_loading=true;history_error=nil;history_deadline=now+45000;history_next_poll=now+5000
 if #history==0 then history_signature=nil;history_dirty=true end
 local launched,err=thread.start(scripts..'/history_worker.lua',{url=cfg.pi_url,queue=queue,generation=history_generation,revision=history_revision},{name='unveil_history',exclusive='unveil_history',replace=false,timeout_ms=40000,defer=true})
 if not launched then history_loading=false;history_error=tostring(err);history_dirty=true end
end
local function box(parent,x,y,w,h,color,radius)
 local o=lvgl.object(parent,{x=x,y=y,w=w,h=h,bg_color=color or C.bg,radius=radius or 0,border_width=0,pad=0})
 o:clear_flag('SCROLLABLE');o:clear_flag('CLICKABLE');return o
end
local function text(parent,s,x,y,w,h,font,color)
 local o=localize_label(lvgl.label(parent,{text=s,x=x,y=y,w=w,h=h,font=font or F.body,text_color=color or C.fg}),s)
 o:clear_flag('CLICKABLE');return o
end
local function center(parent,s,y,font,color)
 local o=localize_label(lvgl.label(parent,{text=s,font=font or F.body,text_color=color or C.fg}),s);o:align('top_mid',0,y);o:clear_flag('CLICKABLE');return o
end
local function button(parent,s,x,y,w,h,fn,selected,font)
 local b=box(parent,x,y,w,h,C.bg,8);b:set_style({border_width=1,border_color=selected and C.orange or C.line});b:add_flag('CLICKABLE')
 local l=center(b,s,0,font or F.body,selected and C.orange or C.fg);l:align('center',0,0)
 if lvgl.guard_tap then lvgl.guard_tap(b,18) end;b:on('clicked',fn)
 return {set_text=function(_,s) l:set_text(s);l:align('center',0,0) end,
 show=function(_,v) if v then b:clear_flag('HIDDEN') else b:add_flag('HIDDEN') end end}
end
local function retry_icon(parent,fn)
 local b=box(parent,218,338,44,44,C.bg,22);b:set_style({border_width=1,border_color=C.orange});b:add_flag('CLICKABLE');if lvgl.guard_tap then lvgl.guard_tap(b,18) end;b:on('clicked',fn)
 local points={}
 for i=0,30 do local a=math.rad(35+i*290/30);points[#points+1]={x=math.floor(22+12*math.cos(a)+.5),y=math.floor(22+12*math.sin(a)+.5)} end
 local tip=points[#points];local a=math.rad(325);local tx,ty=-math.sin(a),math.cos(a);local nx,ny=-ty,tx
 local arrow=lvgl.line(b,{points=points,line_color=C.orange,line_width=2});arrow:clear_flag('CLICKABLE')
 local head=lvgl.line(b,{points={{x=math.floor(tip.x-6*tx+4*nx+.5),y=math.floor(tip.y-6*ty+4*ny+.5)},tip,{x=math.floor(tip.x-6*tx-4*nx+.5),y=math.floor(tip.y-6*ty-4*ny+.5)}},line_color=C.orange,line_width=2});head:clear_flag('CLICKABLE')
 return {show=function(_,v) if v then b:clear_flag('HIDDEN') else b:add_flag('HIDDEN') end end}
end
request_chart=function(source,capture,prepare_job)
 if not source and not (session_started and (state.captured or 0)>0) then chart_data=nil;details_signature=nil;return end
 chart_epoch=chart_epoch+1;chart_data={loading=true};details_signature=nil
 local launched,err=thread.start(scripts..'/chart_worker.lua',{url=cfg.pi_url,job=source or state.chart_job or state.job or '',capture=capture or (not source and state.chart_capture) or '',queue=queue,app=app,dark=cfg.dark,generation=chart_epoch,prepare_job=prepare_job},{name='unveil_chart',exclusive='unveil_chart',replace=true,timeout_ms=40000,defer=true})
 if not launched then chart_data={error=tostring(err)} end
end
local function open(kind,values,key)
 print('Unveil sub='..kind);sub={kind=kind,values=values,key=key};if radar and radar.pause then radar:pause() end;dirty=true
 if kind=='chart' then request_chart() end
end
local function back()
 if wifi_page then
  local handled=false
  local function go() if wifi_page:back() then handled=true;overlay_pager:set_tile_by_index(1,2,false) end end
  if lvgl.batch then lvgl.batch(go) else go() end
  if handled then return end
 end
 print('Unveil sub=none');sub=nil;if radar and radar.resume then radar:resume() end;dirty=true end
local function row(parent,title,value,y,fn,height)
 local h=height or 76
 local b=box(parent,24,y,432,h,C.bg,0)
 local rule=lvgl.line(b,{points={{x=0,y=h-1},{x=432,y=h-1}},line_color=C.line,line_width=1});rule:clear_flag('CLICKABLE')
 local title_label=text(b,title,0,math.floor((h-40)/2),255,40,F.body)
 local value_label
 if value then value_label=localize_label(lvgl.label(b,{text=value,font=F.body,text_color=C.orange}),value);value_label:align('right_mid',0,0) end
 if fn then b:add_flag('CLICKABLE');if lvgl.guard_tap then lvgl.guard_tap(b,18) end;b:on('clicked',fn) end
 return b,value_label,title_label
end
local function head(parent,title)
 center(parent,title,42,F.body,C.fg)
 local rule=lvgl.line(parent,{points={{x=24,y=79},{x=456,y=79}},line_color=C.line,line_width=1});rule:clear_flag('CLICKABLE')
end
local function fixed_header()
 if building_primary then return end
 if not brand_bar or not brand_bar:is_valid() or brand_screen~=screen then
  if brand_bar and brand_bar:is_valid() then brand_bar:delete() end
  brand_screen=screen;brand_bar=box(screen,0,0,480,32,C.bg)
  brand_label=center(brand_bar,'Unveil',0,F.brand,C.muted)
 end
 if not brand_label or not brand_label:is_valid() then brand_label=center(brand_bar,'Unveil',0,F.brand,C.muted) end
 brand_bar:set_pos(0,0);brand_bar:set_size(480,32);brand_bar:clear_flag('HIDDEN');brand_bar:move_foreground()
 brand_label:set_text('Unveil');brand_label:set_style({text_color=C.muted});brand_label:clear_flag('HIDDEN');brand_label:align('top_mid',0,0)
end
local function footer()
 fixed_header()
 local bar=box(screen,0,440,480,40,C.bg)
 nav_items={};nav_selected=nil
 for i=1,5 do
  local x=176+(i-1)*32
  local hit=box(bar,x-16,0,32,40,C.bg);hit:set_style({bg_opa=0});hit:add_flag('CLICKABLE')
  if lvgl.guard_tap then lvgl.guard_tap(hit,18) end;hit:on('clicked',function()
   if not sub and primary_pager then page=i;select_indicator(i,false);primary_pager:set_tile_by_index(i,1,true) end
  end)
  nav_items[i]={dot=box(bar,x-4,12,8,8,C.line,4),active=box(bar,x-10,12,20,8,C.orange,4)}
 end
 nav_marker=nil
 select_indicator(page,true)
end
local function display_percent()
 if scan_start_pending then return 0 end
 local value=state.percent
 if type(value)~='number' then return nil end
 if chart_prepare_job and chart_prepare_job==state.job then return 90+10*chart_prepare_progress end
 return value*.9
end
local function state_name(s)
 return ({failed=tr('检测失败','Failed'),offline=tr('未连接','Offline'),ready=tr('准备就绪','Ready'),scanning=tr('扫频中','Sweeping'),capturing=tr('采集中','Capturing'),analyzing=tr('分析中','Analyzing'),safe=tr('本轮未检出','No detection'),alert=tr('检测到目标','Detected'),saved=tr('已保存','Saved'),incomplete=tr('未完成','Incomplete')})[s] or '—'
end
paint_details=function(parent)
 local info=chart_data
 local signature=tostring(info and (info.chart_token or info.loading or info.error))..'|'..chart_view
 if details_signature==signature and details_ui and details_ui.parent==parent and details_ui.image:is_valid() then return end
 local started=system.millis()
 details_signature=signature
 if not details_ui or details_ui.parent~=parent or not details_ui.image:is_valid() then
  parent:clean();local ui={parent=parent,pills={},labels={},axes={}};details_ui=ui
  for i,name in ipairs({'SPECTRUM','STFT','ACTIVITY'}) do
   local pill=box(parent,28+(i-1)*146,48,132,44,C.card,22);pill:add_flag('CLICKABLE');lvgl.guard_tap(pill,18)
   local label=center(pill,name,0,F.tabs,C.fg);label:align('center',0,0);ui.pills[i]=pill;ui.labels[i]=label
   pill:on('clicked',function() if chart_view==i then return end;local function update() chart_view=i;details_signature=nil;paint_details(parent) end;if lvgl.batch then lvgl.batch(update) else update() end end)
  end
  ui.image=lvgl.image(parent,{x=19,y=102,w=442,h=338});ui.image:clear_flag('CLICKABLE')
  ui.axes[1]=text(parent,'',5,103,33,18,F.axis,C.muted);ui.axes[2]=text(parent,'',5,402,33,18,F.axis,C.muted)
  ui.axes[3]=text(parent,'',5,120,33,18,F.axis,C.muted);ui.axes[4]=text(parent,'',39,414,90,18,F.axis,C.muted)
  ui.axes[5]=lvgl.label(parent,{text='',font=F.axis,text_color=C.muted});ui.axes[5]:align('top_right',-25,414);ui.axes[5]:clear_flag('CLICKABLE')
  ui.placeholder=center(parent,'',210,F.digits,C.muted)
 end
 local ui=details_ui
 for i=1,3 do ui.pills[i]:set_style({bg_color=i==chart_view and C.orange or C.card});ui.labels[i]:set_style({text_color=i==chart_view and '#171619' or C.fg}) end
 local token=info and info.chart_token;local valid=token and token>0
 if valid then valid=ui.image:set_chart(chart_view,token,cfg.dark) end
 if valid then
  ui.image:clear_flag('HIDDEN');ui.placeholder:add_flag('HIDDEN')
  for _,l in ipairs(ui.axes) do l:clear_flag('HIDDEN') end
  local frequency_y=chart_view==2
  local high=frequency_y and string.format('%.0f',((info.center_hz or 0)+(info.sample_rate or 0)/2)/1e6) or string.format('%g',chart_view==1 and (info.spectrum_max or 0) or (info.activity_max or 0))
  local low=frequency_y and string.format('%.0f',((info.center_hz or 0)-(info.sample_rate or 0)/2)/1e6) or string.format('%g',chart_view==1 and (info.spectrum_min or -80) or (info.activity_min or -20))
  local lo=chart_view==1 and ((info.center_hz or 0)-(info.sample_rate or 0)/2)/1e6 or 0
  local hi=chart_view==1 and ((info.center_hz or 0)+(info.sample_rate or 0)/2)/1e6 or info.seconds or 0
  local values={high,low,frequency_y and 'MHz' or 'dB',string.format('%g',lo),string.format('%g %s',hi,chart_view==1 and 'MHz' or 's')}
  for i,v in ipairs(values) do ui.axes[i]:set_text(v) end;ui.axes[5]:align('top_right',-25,414)
 else
  ui.image:add_flag('HIDDEN');for _,l in ipairs(ui.axes) do l:add_flag('HIDDEN') end
  if info then ui.placeholder:set_text(info.loading and '...' or '--');ui.placeholder:clear_flag('HIDDEN') else ui.placeholder:add_flag('HIDDEN') end
  if token and token>0 then chart_data=nil;details_signature=nil;request_chart(info.source_job,info.capture_id) end
 end
 fixed_header();print('Unveil chart view='..chart_view..' update_ms='..(system.millis()-started))
end
local function set_visible(o,v) if v then o:clear_flag('HIDDEN') else o:add_flag('HIDDEN') end end
local function refresh_actual()
 if dirty or sub or paging or not home then return end
 local kind=state.state or 'offline'
 local sig=table.concat({kind,tostring(state.running),tostring(state.online),tostring(state.percent),tostring(state.captured),tostring(state.analyzed),tostring(state.selected),tostring(state.mode),tostring(cfg.analysis_seconds),tostring(chart_prepare_progress),tostring(scan_visual_wait)},'|')
 if sig==last_signature then return end
 last_signature=sig
 if connection_label then connection_label:set_text(state.online and tr('在线','Online') or tr('离线','Offline'));connection_label:align('right_mid',0,0) end
 local running=state.online and state.running
 local words={failed='ERROR',offline='OFFLINE',ready='START',safe='SAFE',alert='ALERT',saved='SAVED',incomplete='RETRY'}
 local finished=kind=='alert' or kind=='safe' or kind=='saved'
 local preparing=finished and chart_prepare_job==state.job and chart_prepare_progress<1
 local total=display_percent()
 if painted_alert~=(kind=='alert' and not preparing) then
  painted_alert=kind=='alert' and not preparing;radar:set_mode(painted_alert)
  readout:set_style({text_color=painted_alert and '#fff8f2' or C.orange});percent_label:set_style({text_color=painted_alert and '#fff8f2' or C.orange});phase:set_style({text_color=painted_alert and '#fff8f2' or C.muted})
  for _,l in ipairs({capture_metric,analysis_metric}) do l:set_style({text_color=painted_alert and '#fff8f2' or C.orange}) end
  for _,l in ipairs(metric_labels) do l:set_style({text_color=painted_alert and '#fff8f2' or C.muted}) end
 end
 local complete=session_started and state.online and finished and not preparing and total==100

 local value=scan_start_pending and '0' or not session_started and not scan_requested and 'START' or (finished or running) and (total and tostring(math.floor(total+.001)) or '--') or words[kind] or 'RETRY'
 if shown_main~=value then
  shown_main=value
  readout:set_style({font=value:match('^%d+$') and F.digits or F.words})
  if readout.set_text_motion then readout:set_text_motion(value,scan_start_pending and 0 or 240) else readout:set_text(value) end
  readout:align('center',0,0)
  local right,bottom=320,270;if readout.center_ink then right,bottom=readout:center_ink() end
  if value:match('^%d+$') then percent_label:set_pos(math.floor(right+5),math.floor(bottom-24));percent_label:clear_flag('HIDDEN') else percent_label:add_flag('HIDDEN') end
 end
 local phase_value=scan_start_pending and tr('启动中','Starting') or preparing and tr('准备图表','Preparing charts') or state_name(kind)
 if shown_phase~=phase_value then shown_phase=phase_value;phase:set_text(phase_value);phase:align('top_mid',0,285) end
 action:show(not running and finished and (not preparing or chart_prepare_failed))
 radar:set_sweeping((running or preparing or scan_start_pending) and not scan_visual_wait)
 local fraction=math.max(0,math.min(100,total or 0))/100
 local selected=state.selected or 0
 local captured=state.capture_percent and state.capture_percent/100 or selected>0 and math.min(1,(state.captured or 0)/selected) or 0
 local analysis_off=state.mode=='collect' or (not state.mode and cfg.analysis_seconds==0)
 local analyzed=not analysis_off and (state.analysis_percent and state.analysis_percent/100 or selected>0 and math.min(1,(state.analyzed or 0)/selected) or 0) or 0
 radar:set_progress(fraction,running)
 local cv=scan_start_pending and '0%' or selected>0 and tostring(math.floor(captured*100+.5))..'%' or '--'
 local av=scan_start_pending and not analysis_off and '0%' or not analysis_off and selected>0 and tostring(math.floor(analyzed*100+.5))..'%' or '--'
 if shown_capture~=cv then shown_capture=cv;capture_metric:set_text(cv);capture_metric:align('center',0,0) end
 if shown_analysis~=av then shown_analysis=av;analysis_metric:set_text(av);analysis_metric:align('center',0,0) end
 if page==3 and paint_details then paint_details(primary_tiles[3]) end
 completion:rendered(state.job,complete,page==1,complete and lvgl.render_stats(false).frame_sequence or 0)
end
local function refresh()
 if lvgl.batch then lvgl.batch(refresh_actual) else refresh_actual() end
end
local function paint_home(parent)
 shown_main=nil;shown_phase=nil;shown_capture=nil;shown_analysis=nil
 home=parent;painted_alert=false
 radar=Radar.new(parent,{bg=C.bg,orange=C.orange,muted=C.muted,dark=cfg.dark});radar:set_mode(painted_alert)
 local hit=box(parent,76,76,327,327,C.bg,164);hit:set_style({bg_opa=0});hit:add_flag('CLICKABLE')
 if lvgl.guard_tap then lvgl.guard_tap(hit,18) end;hit:on('clicked',function()
  if state.running then return end
  if chart_prepare_job==state.job and chart_prepare_progress<1 then if chart_prepare_failed then request_chart(nil,nil,state.job) end;return end
  if not state.online then
   page=5;local connected=true
   if lvgl.wifi then local ok,v=pcall(function() return json.decode(lvgl.wifi('status')) end);if ok then connected=v.connected end end
   open(connected and 'address' or 'wifi')
  elseif state.state=='safe' or state.state=='alert' or state.state=='saved' then page=3;pager:set_tile_by_index(3,1,true)
  else request_scan() end
 end)
 readout=center(parent,'',200,F.words,painted_alert and '#fff8f2' or C.orange)
 percent_label=text(parent,'%',0,0,32,32,F.tabs,painted_alert and '#fff8f2' or C.orange);percent_label:add_flag('HIDDEN')
 metric_labels={center(parent,tr('采集','Capture'),141,F.micro,C.muted),center(parent,tr('分析','Analysis'),141,F.micro,C.muted)}
 metric_labels[1]:align('top_mid',-70,141);metric_labels[2]:align('top_mid',70,141)
 metric_boxes={box(parent,126,160,88,28,C.bg),box(parent,266,160,88,28,C.bg)}
 for _,b in ipairs(metric_boxes) do b:set_style({bg_opa=0}) end
 capture_metric=center(metric_boxes[1],'--',0,F.metric,C.orange);analysis_metric=center(metric_boxes[2],'--',0,F.metric,C.orange)
 capture_metric:align('center',0,0);analysis_metric:align('center',0,0)
 capture_metric:set_style({pad=2});analysis_metric:set_style({pad=2})
 phase=center(parent,'',285,F.status,painted_alert and '#fff8f2' or C.muted)
 action=retry_icon(parent,function() if not state.running then if chart_prepare_job==state.job and chart_prepare_progress<1 then request_chart(nil,nil,state.job) else start('start') end end end)

end
paint_history=function(parent,shell_only)
 local signature=tostring(cfg.language)..tostring(cfg.history_newest)..tostring(#history==0 and (history_loading or not history_loaded))..tostring(history_error)..(shell_only and ':shell' or '')..json.encode(history)
 if history_parent==parent and history_signature==signature then history_dirty=false;return end
 local started=system.millis();history_parent=parent;history_signature=signature
 local function update()
  if not history_ui or history_ui.parent~=parent or not history_ui.list:is_valid() then
   parent:clean();local ui={parent=parent,rows={},count=0};history_ui=ui
   ui.sort=box(parent,162,48,156,38,C.card,19);ui.sort:add_flag('CLICKABLE');lvgl.guard_tap(ui.sort,18)
   ui.title=center(ui.sort,'',0,F.tabs,C.fg);ui.title:align('center',0,0)
   ui.list=lvgl.object(parent,{x=0,y=100,w=468,h=334,pad=0,radius=0,bg_color=C.bg,border_width=0})
   ui.list:add_flag('SCROLLABLE');ui.list:set_scroll({dir='ver',scrollbar='auto'})
   ui.spacer=box(ui.list,0,0,1,1,C.bg);ui.spacer:set_style({bg_opa=0})
   ui.empty=center(parent,tr('暂无记录','No records'),220,F.body,C.muted)
   function ui:fill(force)
    local first=math.max(1,math.min(math.max(1,self.count-7),math.floor(math.max(0,self.list:scroll_y())/84)))
    if not force and self.first==first then return end;self.first=first
    for i=1,math.min(8,self.count) do
     local index=first+i-1;local n=cfg.history_newest and #history-index+1 or index;local h=history[n];local slot=self.rows[i]
     if not slot then
      slot={};self.rows[i]=slot
      slot.item,slot.state,slot.number=row(self.list,'','',0,function() local record=slot.pressed_record or slot.record;slot.pressed_record=nil;if record then open('record');sub.record=record end end)
      slot.item:on('pressed',function() slot.pressed_record=slot.record end)
      slot.time=text(slot.item,'',58,32,195,20,F.dialLabel,cfg.dark and '#8d949e' or '#707780')
     end
     slot.record=h;slot.item:clear_flag('HIDDEN');slot.item:set_pos(24,(index-1)*84)
     local number,value,timestamp=string.format('%02d',n),state_name(h.state),h.display_time or ''
     if slot.shown_number~=number then slot.shown_number=number;slot.number:set_text(number) end
     if slot.shown_state~=value then slot.shown_state=value;slot.state:set_text(value);slot.state:align('right_mid',0,0) end
     if slot.shown_time~=timestamp then slot.shown_time=timestamp;slot.time:set_text(timestamp) end
    end
    for i=math.min(8,self.count)+1,#self.rows do self.rows[i].item:add_flag('HIDDEN') end
    print('Unveil history viewport first='..first..' rows='..math.min(8,self.count))
   end
   ui.list:on('scroll',function() if page==4 and not sub and not paging then if lvgl.batch then lvgl.batch(function() ui:fill(false) end) else ui:fill(false) end end end)
   ui.sort:on('clicked',function() cfg.history_newest=not cfg.history_newest;save();ui.list:scroll_y(0);history_signature=nil;paint_history(parent) end)
  end
  local ui=history_ui;ui.count=shell_only and 0 or #history
  ui.title:set_text(cfg.history_newest and 'NEWEST' or 'OLDEST');ui.title:align('center',0,0)
  ui.spacer:set_size(1,math.max(1,ui.count*84))
  if ui.count==0 and not shell_only then
   ui.empty:set_text(history_error and '--' or (history_loading or not history_loaded) and '...' or tr('暂无记录','No records'));ui.empty:clear_flag('HIDDEN')
  else ui.empty:add_flag('HIDDEN') end
  ui:fill(true)
 end
 if lvgl.batch then lvgl.batch(update) else update() end
 history_dirty=not not shell_only and #history>0
 print('Unveil history rendered='..#history..' newest='..tostring(cfg.history_newest)..' update_ms='..(system.millis()-started))
end
local function paint_primary(parent,index)
 if index==1 then paint_home(parent);return end
 if index==2 then settings_controller=SettingsDial.new(parent,cfg,F,C,tr,save,localize_label);return end
 if index==3 then details_signature=nil;paint_details(parent);return end
 if index==4 then paint_history(parent,false);return end
 head(parent,tr('系统','System'))
 local list=lvgl.object(parent,{x=0,y=86,w=468,h=348,pad=0,radius=0,bg_color=C.bg,border_width=0})
 list:add_flag('SCROLLABLE');list:set_scroll({dir='ver',scrollbar='auto'})
 do
  local function system_row(title,value,y,fn) local b,l=row(list,title,value,y-86,fn,64);if l then system_values[y]=l end;return b end
  local wifi='--';if lvgl.wifi then local ok,v=pcall(function() return json.decode(lvgl.wifi('status')) end);if ok then wifi=v.enabled and 'ON' or 'OFF' end end
  system_row('Wi-Fi',wifi,86,function() open('wifi') end)
  system_row(tr('主题','Theme'),cfg.dark and tr('深色','Dark') or tr('浅色','Light'),150,function() open('theme',{{value=false,text=tr('浅色','Light')},{value=true,text=tr('深色','Dark')}},'dark') end)
  system_row(tr('语言','Language'),cfg.language=='zh' and '中文' or 'English',214,function() open('language',{{value='zh',text='中文'},{value='en',text='English'}},'language') end)
  system_row(tr('服务地址','Server'),cfg.pi_url=='' and tr('未设置','Not set') or tr('已设置','Set'),278,function() open('address') end)
  system_row(tr('声音','Sound'),cfg.sound_muted and tr('静音','Muted') or tostring(cfg.sound_volume)..'%',342,function() open('sound') end)
  local connection_row=system_row(tr('连接','Connection'),nil,406,nil)
  connection_label=lvgl.label(connection_row,{text=state.online and tr('在线','Online') or tr('离线','Offline'),font=F.body,text_color=C.orange});connection_label:align('right_mid',0,0)
 end
end
local function save_endpoint()
 local url=Endpoint.normalize(edit_value,edit_scheme)
 if not url then editor:set_text(tr('地址无效','Invalid address'));return end
 cfg.pi_url=url;session_started=false;chart_data=nil;load_history();save();state={online=false,state='offline'};pending=false;last_signature=nil;back()

end
local function paint_sub(parent)
 local titles={mode=tr('运行模式','Mode'),rounds=tr('扫频轮数','Sweeps'),capture=tr('采集时长','Capture'),analysis=tr('分析窗口','Analysis'),theme=tr('主题','Theme'),language=tr('语言','Language'),address=tr('服务地址','Server'),wifi='Wi-Fi',sound=tr('声音','Sound'),chart=tr('时频图','Spectrogram'),record=tr('采集记录','Capture')}
 head(parent,titles[sub.kind] or '')
 if sub.values then
  local n=#sub.values;local h=n>4 and 60 or 76;local gap=10
  for i,v in ipairs(sub.values) do button(parent,v.text,24,86+(i-1)*(h+gap),432,h,function() cfg[sub.key]=v.value;save();back() end,cfg[sub.key]==v.value,F.body) end
 elseif sub.kind=='sound' then
  local mute_button
  mute_button=button(parent,tr('静音','Mute')..'  '..(cfg.sound_muted and 'ON' or 'OFF'),40,106,400,64,function()
   cfg.sound_muted=not cfg.sound_muted;save();mute_button:set_text(tr('静音','Mute')..'  '..(cfg.sound_muted and 'ON' or 'OFF'))
   if lvgl.sound then pcall(lvgl.sound,'settings',cfg.sound_volume,cfg.sound_muted) end
  end,false,F.body)
  center(parent,tr('音量','Volume'),208,F.body,C.fg)
  local volume=center(parent,tostring(cfg.sound_volume)..'%',256,F.volume,C.orange)
  local slider=lvgl.slider(parent,{x=60,y=332,w=360,h=16,min=0,max=100,value=cfg.sound_volume})
  slider:clear_flag('SCROLLABLE');if slider.set_palette then slider:set_palette(tonumber(C.line:sub(2),16),tonumber(C.orange:sub(2),16)) end
  slider:on('value_changed',function()
   cfg.sound_volume=slider:get_value();volume:set_text(tostring(cfg.sound_volume)..'%');volume:align('top_mid',0,256)
   if lvgl.sound then pcall(lvgl.sound,'settings',cfg.sound_volume,cfg.sound_muted) end
  end)
  slider:on('released',function() save();if not cfg.sound_muted then play_sound('safe') end end)
 elseif sub.kind=='wifi' then
  wifi_page=WiFi.new(parent,F,C,tr)
 elseif sub.kind=='address' then
  edit_scheme=cfg.pi_url:match('^https://') and 'https://' or 'http://';edit_value=cfg.pi_url:gsub('^https?://','')
  editor_scheme=button(parent,edit_scheme,318,88,138,44,function() edit_scheme=edit_scheme=='http://' and 'https://' or 'http://';editor_scheme:set_text(edit_scheme) end,false,F.small)
  editor=text(parent,edit_value,24,92,286,44,F.small)
  local keypad=box(parent,20,144,440,222,C.bg)
  local layouts={'abcdefghijklmnopqrstuvwxyz.-','ABCDEFGHIJKLMNOPQRSTUVWXYZ.-','1234567890.:-/'}
  local layout=1
  local mode_button
  local keys={}
  local function redraw_keys()
   local chars=layouts[layout]
   for i=1,#chars do
    local key=chars:sub(i,i)
    if not keys[i] then
     local slot=i
     keys[i]=button(keypad,key,((i-1)%7)*63,math.floor((i-1)/7)*55,57,49,function()
      local current=layouts[layout]:sub(slot,slot)
      if #edit_value<253 then edit_value=edit_value..current end;editor:set_text(edit_value)
     end,false,F.body)
    end
    keys[i]:set_text(key);keys[i]:show(true)

   end
   for i=#chars+1,#keys do keys[i]:show(false) end
  end
  mode_button=button(parent,'ABC',20,376,84,48,function() layout=layout%3+1;mode_button:set_text(({'ABC','123','abc'})[layout]);redraw_keys() end,false,F.small)
  button(parent,tr('删除','Del'),110,376,80,48,function() edit_value=edit_value:sub(1,-2);editor:set_text(edit_value) end,false,F.small)
  button(parent,tr('清空','Clear'),196,376,80,48,function() edit_value='';editor:set_text(edit_value) end,false,F.small)
  button(parent,tr('默认','Default'),282,376,80,48,function() edit_scheme='https://';edit_value=Endpoint.default:gsub('^https://','');editor:set_text(edit_value);editor_scheme:set_text(edit_scheme) end,false,F.small)
  button(parent,tr('保存','Save'),368,376,92,48,save_endpoint,true,F.small)
  redraw_keys()
 elseif sub.kind=='chart' then
  local info=chart_data
  if info and info.file then
   center(parent,string.format('%.2f MHz · %g s',(info.center_hz or 0)/1e6,info.seconds or 0),90,F.small,C.muted)
   local chart=lvgl.image(parent,{x=19,y=96,w=442,h=340});chart:set_raw_rgb565(442,340,storage.read_file(info.file));chart:clear_flag('CLICKABLE')
   center(parent,tr('已保存 I/Q','Saved I/Q'),335,F.small,C.muted)
  else center(parent,info and info.loading and '...' or '--',210,F.body,C.muted) end
 elseif sub.kind=='record' then
  local h=sub.record
  button(parent,tr('图表','Charts'),150,365,180,44,function() local source=h.chart_job or h.job;sub=nil;if radar and radar.resume then radar:resume() end;page=3;dirty=true;request_chart(source,h.chart_capture);primary_pager:set_tile_by_index(3,1,false) end,false,F.small)
  center(parent,state_name(h.state),115,F.title,C.orange)
  text(parent,string.format('%s  %d / %d\n%s  %d / %d',tr('采集','Captured'),h.captured or 0,h.selected or 0,tr('分析','Analyzed'),h.analyzed or 0,h.selected or 0),40,210,400,120,F.body)
 end
 local return_zone=box(parent,0,434,480,46,C.bg);return_zone:set_style({bg_opa=0});return_zone:add_flag('CLICKABLE')
 box(parent,204,466,72,4,C.muted,2)
end
local function build_primary()
 if settings_controller then settings_controller:flush(system.millis(),true);settings_controller=nil;collectgarbage('collect') end
 building_primary=true
 local old_screen=primary_pager and screen or nil
 if old_screen then screen=lvgl.create_screen();screen:clear_flag('SCROLLABLE') end
 C=cfg.dark and {bg='#000000',fg='#eef0f4',muted='#b5bdca',line='#5b6370',orange='#f54b2c',card='#343940'} or {bg='#e1e1e6',fg='#292c33',muted='#59616e',line='#b0b5be',orange='#ed4a2e',card='#f4f2f4'}
 screen:set_style({bg_color=C.bg,pad=0})
 localized_labels={};home=nil;system_values={};history_ui=nil;details_ui=nil;history_signature=nil;history_parent=nil;details_signature=nil;connection_label=nil;overlay=nil;overlay_pager=nil;last_signature=nil
 primary_pager=lvgl.tileview(screen,{x=0,y=0,w=480,h=440,pad=0,border_width=0,bg_color=C.bg})
 primary_pager:set_scroll({dir='hor',scrollbar='off',snap_x='center',snap_y='center'})
 if primary_pager.pager_motion then primary_pager:pager_motion() end
 pcall(function() primary_pager:on('scroll_begin',function() paging=true;if lvgl.render_stats then lvgl.render_stats(true) end; end) end)
 pcall(function() primary_pager:on('scroll_end',function() paging=false;fixed_header();if lvgl.render_stats then local p=lvgl.render_stats(true);print(string.format('Unveil paging frames=%d mean_us=%d max_us=%d elapsed_ms=%d',p.frames,p.mean_us,p.max_us,p.elapsed_ms)) end;last_signature=nil;refresh() end) end)
 pager=primary_pager;primary_tiles={}
 local this_pager=primary_pager
 for i=1,5 do
  local tile=primary_pager:add_tile(i,1,i==1 and 'right' or i==5 and 'left' or 'hor')
  tile:set_style({bg_color=C.bg,pad=0,border_width=0});tile:clear_flag('SCROLLABLE')
  local content=box(tile,0,0,480,480,C.bg);primary_tiles[i]=content;paint_primary(content,i)
 end
 primary_pager:set_tile_by_index(page,1,false)
 primary_pager:on('value_changed',function()
  if this_pager~=primary_pager or not this_pager:is_valid() or sub then return end
  local tile=this_pager:get_active_tile();if tile then local x=tile:get_pos();page=math.max(1,math.min(5,math.floor(x/480+.5)+1));select_indicator(page,false);if page==3 and not chart_data then request_chart() end;if page==4 then request_history();dirty=true end;last_signature=nil;refresh() end
 end)
 footer();built_dark=cfg.dark;built_language=cfg.language
 dirty=false;refresh();screen:load()
 if old_screen then old_screen:delete() end
 building_primary=false;brand_bar=nil;brand_label=nil;brand_screen=nil;fixed_header()
end
local function render_actual()
 if not primary_pager or built_dark~=cfg.dark then build_primary()
 elseif built_language~=cfg.language then update_language() end
 if sub then
  if overlay then overlay:delete() end
  primary_pager:set_tile_by_index(page,1,false);select_indicator(page,true)
  overlay=box(screen,0,0,480,480,C.bg);overlay:set_style({bg_opa=0})
  overlay_pager=lvgl.tileview(overlay,{x=0,y=0,w=480,h=480,pad=0,border_width=0,bg_opa=0})
  overlay_pager:set_scroll({dir='ver',scrollbar='off',snap_x='center',snap_y='center'})
  local current_overlay=overlay_pager
  for i=1,3 do
   local tile=overlay_pager:add_tile(1,i,'ver');tile:set_style({bg_color=C.bg,bg_opa=i==2 and 255 or 0,pad=0,border_width=0});tile:clear_flag('SCROLLABLE')
   if i==2 then paint_sub(tile) end
  end
  overlay_pager:set_tile_by_index(1,2,false)
  overlay_pager:on('value_changed',function()
   if current_overlay~=overlay_pager or not current_overlay:is_valid() then return end
   local tile=current_overlay:get_active_tile();if tile then local _,y=tile:get_pos();if y>490 then back() elseif y<470 then overlay_pager:set_tile_by_index(1,2,true) end end
  end)
 elseif overlay then
  -- Parent stays alive throughout the gesture; only replace the changed rows.
  if page==4 then paint_history(primary_tiles[4]) elseif page==5 then
   local values={[150]=cfg.dark and tr('深色','Dark') or tr('浅色','Light'),[214]=cfg.language=='zh' and '中文' or 'English',[278]=tr('已设置','Set'),[342]=cfg.sound_muted and tr('静音','Muted') or tostring(cfg.sound_volume)..'%'}
   if lvgl.wifi then local ok,v=pcall(function() return json.decode(lvgl.wifi('status')) end);if ok then values[86]=v.enabled and 'ON' or 'OFF' end end
   for y,value in pairs(values) do local l=system_values[y];if l and l:is_valid() then l:set_text(value);l:align('right_mid',0,0) end end
  end
  overlay:delete();overlay=nil;overlay_pager=nil;pager=primary_pager;last_signature=nil
  if wifi_page then wifi_page:close();wifi_page=nil;collectgarbage('collect') end
 elseif page==4 then
  paint_history(primary_tiles[4])
 end
 if lvgl.shell_exit then lvgl.shell_exit(not sub) end
 fixed_header();dirty=false;refresh()
end
local function render()
 local started=system.millis();local rebuilding=not primary_pager or built_dark~=cfg.dark
 if lvgl.batch then lvgl.batch(render_actual) else render_actual() end
 print('Unveil UI page='..page..' sub='..(sub and sub.kind or 'none')..' rebuild='..tostring(rebuilding)..' update_ms='..(system.millis()-started))
end
local function startup_stage(phase)
 local i=system.info and system.info() or {};pcall(system.publish_status,'launch-status.json',json.encode({phase=phase,sram_free=i.sram_free,sram_largest=i.sram_largest,psram_free=i.psram_free}))
end
local function run()
 startup_stage('before_init');lvgl.init({hold_first_frame=true,unveil_input=true});owned=true;if lvgl.chart_clear then lvgl.chart_clear() end
 screen=lvgl.create_screen();screen:set_style({bg_color=cfg.dark and '#000000' or '#e1e1e6',bg_opa=255,pad=0});screen:clear_flag('SCROLLABLE')
 -- A previous request may still be unwinding. Session generations reject its
 -- messages; keep the new UI responsive while the exclusive worker finishes.
 while thread.sync.queue_recv(queue,0) do end
 if thread.sync.queue_delete then thread.sync.queue_delete(queue) end
 local created,queue_error=thread.sync.queue_create(queue,{depth=4,item_size=2048});assert(created or queue_error=='exists',queue_error)
 startup_stage('before_fonts')
 local function load(size,doto) return lvgl.font_load('apps/unveil/assets/'..(doto and 'Doto-Regular.ttf' or 'Unveil-Text-Readable.ttf'),{size=size,cache_size=128}) end
 F={volume=load(36,true),tabs=load(24,true),axis=load(12),status=load(18),dialLabel=load(16),micro=load(14),metric=load(18,true),dial=lvgl.font_load('apps/unveil/assets/Doto-Metrics.ttf',{size=44,cache_size=64}),title=load(36),body=load(28),small=load(24),brand=load(28,true),words=load(72,true),digits=load(96,true)}
 startup_stage('fonts_ready');lvgl.set_default_font(nil);screen:clear_flag('SCROLLABLE')
 local w,h=screen:get_size();assert(w==480 and h==480,'Unveil requires Mosaico 480 x 480')
 render();screen:load();if lvgl.present_ready then local p=lvgl.render_stats and lvgl.render_stats(false);print('Unveil bootstrap early_frames='..tostring(p and p.frames));lvgl.present_ready() end;startup_stage('ui_ready');if lvgl.sound then pcall(lvgl.sound,'settings',cfg.sound_volume,cfg.sound_muted) end;start()
 local next_poll=system.millis()+1500
 if lvgl.set_gesture_options then lvgl.set_gesture_options({scroll_limit=28,scroll_throw=12}) end
 local next_frame=system.millis()
 local next_nav=system.millis()
 local next_command_check=0
 local next_profile=system.millis()+5000
 if lvgl.render_stats then lvgl.render_stats(true) end
 local pacing=delay.periodic and delay.periodic(16) or nil
 while true do
  lvgl.process_events(0)
  local message=thread.sync.queue_recv(queue,0)
  if message then
   local received=json.decode(message)
   if received.kind=='chart_progress' then
    if received.prepare_job and received.prepare_job==chart_prepare_job and received.endpoint==cfg.pi_url then chart_prepare_progress=math.max(chart_prepare_progress,received.fraction or 0);last_signature=nil end
   elseif received.kind=='history' then
    if received.endpoint==cfg.pi_url and received.generation==history_generation then
     for _,item in ipairs(received.records or {}) do history_buffer[#history_buffer+1]=item end
     if received.done then
      history_loading=false;history_deadline=0
      if received.error then
       history_error=received.error;history_dirty=true;print('Unveil history request failed: '..tostring(received.error))
      elseif received.unchanged then
       history_loaded=true;history_error=nil
       if #history==0 and (received.count or 0)>0 then history_loaded=false;history_revision='';history_next_poll=system.millis()+1000 end
       if #history==0 then history_signature=nil;history_dirty=true end
      elseif #history_buffer==tonumber(received.count) then
       history=history_buffer;history_revision=received.revision;history_loaded=true;history_error=nil;history_signature=nil;history_dirty=true
       print('Unveil history committed records='..#history..' generation='..history_generation)
      else
       history_error='incomplete';history_revision='';history_next_poll=system.millis()+1000;history_dirty=true
       print('Unveil history incomplete kept='..#history..' received='..#history_buffer..' expected='..tostring(received.count))
      end
      history_buffer={}
     end
    end
   elseif received.kind=='chart' then
    if received.endpoint==cfg.pi_url and received.generation==chart_epoch then
     if received.prepare_job==chart_prepare_job and received.prepare_job then chart_prepare_failed=not not received.error;chart_prepare_progress=received.chart_token and received.chart_token>0 and 1 or chart_prepare_progress;chart_retry_at=system.millis()+5000 end
     chart_data=received;details_signature=nil;last_signature=nil;if sub and sub.kind=='chart' then dirty=true end end
   else
   if (not received.endpoint or received.endpoint==cfg.pi_url) and received.request_generation==run_generation then
    if received.online and server_test~=(not not received.test) then server_test=not not received.test;chart_data=nil;details_signature=nil end
    if received.command_complete=='start' then scan_start_pending=false end
    if received.start_ack then session_started=true end
    state=received;
    if session_started and not state.running and (state.state=='safe' or state.state=='alert' or state.state=='saved') and (state.captured or 0)>0 and chart_prepare_job~=state.job then
     chart_prepare_job=state.job;chart_prepare_progress=0;chart_prepare_failed=false;chart_retry_at=system.millis()+5000;request_chart(nil,nil,state.job)
    end
    health({test=not not state.test,session_started=session_started,language=cfg.language,percent=display_percent(),backend_percent=state.percent,chart_preparing=chart_prepare_job==state.job and chart_prepare_progress<1,page=page,sub=sub and sub.kind or '',online=state.online,state=state.state,notice=state.notice,endpoint=state.endpoint,job=state.job,captured=state.captured,analyzed=state.analyzed,selected=state.selected}) end;pending=false
   if state.online and not state.running and state.job and state.job~='' then history_next_poll=0 end
   end
   refresh()
  end
  local now=system.millis()
  if lvgl.scan_key and lvgl.scan_key() then
   if settings_controller then settings_controller:flush(now,true) end
   scan_visual_wait=true;if radar then radar:set_sweeping(false) end
   sub=nil;page=1;paging=false;dirty=true;render()
   if primary_pager.pager_home then primary_pager:pager_home() else primary_pager:set_tile_by_index(1,1,false) end
   select_indicator(1,true)
   if radar and radar.resume then radar:resume() end
   local started=request_scan();if not started and not start_feedback then start_feedback=ScanGate.new(false) end;if started then render() else refresh() end
   print('Unveil top-key home scan='..tostring(started)..' running='..tostring(state.running))
   if started then next_poll=now+500 end
  end
  if (not history_loaded or page==4) and not sub and now>=history_next_poll then request_history() end
  if page==1 and chart_prepare_job==state.job and chart_prepare_progress<1 and now>=chart_retry_at then
   local ok,info=thread.get('unveil_chart');local status=ok and info:match('status=(%w+)');if status~='running' and status~='queued' then chart_retry_at=now+5000;request_chart(nil,nil,state.job) end
  end
  if start_feedback then
   if page~=1 or sub then
    start_feedback=nil;scan_visual_wait=false;haptics:cancel_start()
   else
    local settled=not paging and (not primary_pager.pager_home_settled or primary_pager:pager_home_settled())
    local p=lvgl.present_status();local f=start_feedback
    local action=ScanGate.step(f,settled,p)
    if action=='paint' then
     -- Fence a static home frame after all pager movement has stopped.
     primary_pager:invalidate()
     print('Unveil home settled; static frame requested')
    elseif action=='start' then
     print('Unveil home presented frame='..f.frame..' transfer='..p.transferred..' sequence='..p.sequence)
     start_feedback=nil;scan_visual_wait=false;last_signature=nil
     local function release()
      refresh()
      print('Unveil scan animation enabled at='..system.millis())
      if f.haptic then haptics:start_presented() end
     end
     if lvgl.batch then lvgl.batch(release) else release() end
    end
   end
  end
  if completion:pending() then
   local eligible=session_started and state.online and not state.running and display_percent()==100 and (state.state=='safe' or state.state=='alert' or state.state=='saved') and not (chart_prepare_job==state.job and chart_prepare_progress<1)
   completion:tick(state.job,eligible,page==1 and not sub and not paging,lvgl.render_stats(false).frame_sequence)
  end
  haptics:tick()
  persistence:tick(now)
  if wifi_page and sub and sub.kind=='wifi' then wifi_page:tick(now) end
  if lvgl.render_stats and not paging and now>=next_profile then
   next_profile=now+5000
   local perf=lvgl.render_stats(true)
   if perf.frames>1 then print(string.format('Unveil render frames=%d mean_us=%d max_us=%d elapsed_ms=%d acquire_us=%d submit_us=%d commit_us=%d tx_bytes=%d tx_frames=%d rows=%d-%d dma_wait_us=%d te_wait_us=%d',perf.frames,perf.mean_us,perf.max_us,perf.elapsed_ms,perf.acquire_us,perf.submit_us,perf.commit_us,perf.tx_bytes,perf.tx_frames,perf.tx_min_rows,perf.tx_max_rows,perf.dma_wait_us,perf.te_wait_us)) end
  end
  if settings_controller then settings_controller:flush(now,false) end
  if queued_command and now>=next_command_check then
   next_command_check=now+200
   if not active() then pending=false;start(queued_command);next_poll=now+2500 end
  end
  if now>=next_poll and (not sub or state.running) then
   next_poll=now+(state.running and 500 or 5000)
   if not active() then
    if pending then pending=false end
    pending=false;start()
   end
  end
  if history_dirty and not paging and primary_tiles and not sub and (page==4 or not state.running) then paint_history(primary_tiles[4]) end
  if dirty and not paging then render() end
  if not sub and now>=next_nav then
   next_nav=now+500
   local tile=primary_pager:get_active_tile()
   if tile then local x=tile:get_pos();local base=primary_tiles[1]:get_pos();local w=primary_tiles[1]:get_size()
    if w>0 then page=math.max(1,math.min(5,math.floor((x-base)/w+.5)+1));select_indicator(page,true) end
   end
  end
  if now>=next_frame then
   next_frame=now+25
   if not paging and not sub and page==1 and state.online and state.running then
    radar:animate(now)
   end
  end
  if pacing then pacing:wait() else delay.delay_ms(16) end
 end
end
local success,err=xpcall(run,debug.traceback)
if not success then pcall(storage.write_file,app..'/last-error.txt',tostring(err)) end
pcall(thread.stop,job,1)
if owned then pcall(lvgl.deinit) end
if not success then error(err) end
