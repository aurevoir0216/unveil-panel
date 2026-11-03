local M={}
function M.new(clock)
 local self={begin=nil,cycle=0}
 function self:start() self.begin=clock();self.cycle=self.cycle+1 end
 function self:poll()
  local t=self.begin and (clock()-self.begin)/1000 or -1
  local state='ready';local percent=0;local captured,analyzed=0,0
  if t>=0 and t<4 then state='scanning';percent=math.floor(t/4*100)
  elseif t>=4 and t<8 then state='capturing';percent=math.floor((t-4)/4*100);captured=math.floor(percent/50)
  elseif t>=8 and t<12 then state='analyzing';percent=math.floor((t-8)/4*100);captured=2;analyzed=math.floor(percent/50)
  elseif t>=12 then state=self.cycle%2==1 and 'alert' or 'safe';percent=100;captured=2;analyzed=2 end
  if t>=0 then percent=math.min(100,math.floor(t/12*100)) end
  return {online=true,test=true,state=state,running=t>=0 and t<12,percent=percent,captured=captured,analyzed=analyzed,selected=2,job='test_'..self.cycle,mode='scan',notice=''}
 end
 return self
end
return M
