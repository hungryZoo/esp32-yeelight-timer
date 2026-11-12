// 제어 웹페이지 (단일 HTML, 외부 리소스 없음)
#pragma once

static const char WEB_PAGE[] = R"HTML(<!doctype html>
<html lang="ko"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Candela 타이머</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d1f23;--mut:#6b7280;--acc:#e8a33d;--ok:#16a34a;--bad:#dc2626;--bd:#e5e7eb}
@media(prefers-color-scheme:dark){:root{--bg:#111316;--card:#1b1e23;--fg:#e8eaed;--mut:#9aa0a6;--bd:#2c3036}}
*{box-sizing:border-box}
body{margin:0;font:15px/1.45 system-ui,-apple-system,"Apple SD Gothic Neo","Malgun Gothic",sans-serif;background:var(--bg);color:var(--fg)}
main{max-width:480px;margin:0 auto;padding:16px}
header{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:12px}
h1{font-size:18px;margin:0}
#clock{font-variant-numeric:tabular-nums;font-size:22px;font-weight:600}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:14px;margin-bottom:12px}
.card h2{font-size:13px;color:var(--mut);margin:0 0 10px;font-weight:600}
.row{display:flex;align-items:center;justify-content:space-between;gap:8px;margin:10px 0}
.badge{font-size:12px;font-weight:600;padding:2px 10px;border-radius:99px;border:1px solid var(--bd)}
.ok{color:var(--ok);border-color:var(--ok)}.bad{color:var(--bad);border-color:var(--bad)}.warn{color:var(--acc);border-color:var(--acc)}
button{font:inherit;border:1px solid var(--bd);background:var(--card);color:var(--fg);border-radius:10px;padding:8px 14px;cursor:pointer}
button:active{transform:scale(.98)}
.primary{background:var(--acc);border-color:var(--acc);color:#1d1f23;font-weight:600}
#power{width:100%;padding:18px;font-size:18px;font-weight:700}
#power.on{background:var(--acc);border-color:var(--acc);color:#1d1f23}
input[type=range]{width:100%;accent-color:var(--acc)}
input[type=time],input[type=number]{font:inherit;padding:6px 8px;border:1px solid var(--bd);border-radius:8px;background:var(--bg);color:var(--fg)}
.presets{display:flex;gap:6px}.presets button{flex:1;padding:6px}
.mut{color:var(--mut);font-size:13px}
pre{font-size:11px;max-height:280px;overflow:auto;background:var(--bg);padding:8px;border-radius:8px;white-space:pre-wrap;word-break:break-all;margin:8px 0 0}
.sw{position:relative;width:46px;height:26px;flex:none}.sw input{opacity:0;width:0;height:0}
.sl{position:absolute;inset:0;background:var(--bd);border-radius:99px;transition:.2s}
.sl:before{content:"";position:absolute;width:20px;height:20px;left:3px;top:3px;background:#fff;border-radius:50%;transition:.2s}
.sw input:checked+.sl{background:var(--ok)}.sw input:checked+.sl:before{transform:translateX(20px)}
#toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:#333;color:#fff;padding:8px 14px;border-radius:8px;font-size:13px;opacity:0;transition:opacity .3s;pointer-events:none}
</style></head><body><main>
<header><h1>Candela 타이머</h1><span id="clock">--:--</span></header>

<section class="card">
<h2>램프</h2>
<button id="power">-</button>
<div class="row"><span>밝기</span><span id="briVal">-</span></div>
<input id="bri" type="range" min="1" max="100" value="50">
<div class="presets"><button data-b="10">10%</button><button data-b="30">30%</button><button data-b="60">60%</button><button data-b="100">100%</button></div>
<p class="mut" id="pending" hidden></p>
</section>

<section class="card">
<h2>스케줄 (매일 반복)</h2>
<div class="row"><span>스케줄 사용</span><label class="sw"><input id="schEn" type="checkbox"><span class="sl"></span></label></div>
<div class="row"><span>ON 시각</span><input id="schOn" type="time" required></div>
<div class="row"><span>OFF 시각</span><input id="schOff" type="time" required></div>
<div class="row"><span>ON 시 밝기 <span class="mut">(0 = 유지)</span></span><input id="schBri" type="number" min="0" max="100" style="width:80px"></div>
<div class="row"><span class="mut" id="schInfo">-</span><button class="primary" id="schSave">저장</button></div>
</section>

<section class="card">
<h2>연결</h2>
<div class="row"><span>BLE</span><span id="bleBadge" class="badge">-</span></div>
<div class="mut" id="bleInfo">-</div>
<div class="row"><span>WiFi</span><span id="wifiInfo" class="mut">-</span></div>
<div class="row"><button id="pair">페어링</button><button id="refresh">램프 상태 새로고침</button></div>
<p class="mut" id="pairHint" hidden>페어링 진행 중. 램프가 깜빡이면 램프 링을 돌려 확인하세요.</p>
</section>

<section class="card">
<details id="logBox"><summary>로그</summary><pre id="log"></pre></details>
<div class="row"><span class="mut" id="sys">-</span><button id="reboot">재부팅</button></div>
</section>
</main>
<div id="toast"></div>
<script>
const $=id=>document.getElementById(id);
let st=null,briHold=0,schDirty=false,briTimer=null,pollTimer=null;
function toast(t){const e=$('toast');e.textContent=t;e.style.opacity=1;clearTimeout(e._t);e._t=setTimeout(()=>e.style.opacity=0,1800)}
async function api(path,params){
  const r=await fetch(path,params?{method:'POST',body:new URLSearchParams(params)}:{cache:'no-store'});
  if(!r.ok)throw new Error(await r.text());
  return r.json();
}
function dur(ms){const s=Math.floor(ms/1000);if(s<60)return s+'초';if(s<3600)return Math.floor(s/60)+'분';
  const h=Math.floor(s/3600);return h<48?h+'시간 '+Math.floor(s%3600/60)+'분':Math.floor(h/24)+'일'}
function render(s){
  st=s;
  $('clock').textContent=s.timeValid?s.time:'--:--';
  const b=s.ble,p=$('power');
  p.textContent=b.power==1?'켜짐':b.power==0?'꺼짐':'상태 미확인';
  p.classList.toggle('on',b.power==1);
  if(Date.now()>briHold&&b.brightness>0){$('bri').value=b.brightness;$('briVal').textContent=b.brightness+'%'}
  const pend=[];if(b.pendingPower>=0)pend.push(b.pendingPower?'켜기':'끄기');if(b.pendingBrightness>=0)pend.push('밝기 '+b.pendingBrightness+'%');
  $('pending').hidden=!pend.length;$('pending').textContent='BLE 연결 대기, 연결 시 전송: '+pend.join(', ');
  const c=s.schedule;
  if(!schDirty){$('schEn').checked=c.enabled;$('schOn').value=c.on;$('schOff').value=c.off;$('schBri').value=c.onBrightness}
  $('schInfo').textContent=(c.enabled?(c.next?'다음 '+c.next:'시각 미확보'):'스케줄 꺼짐')+(c.last!='-'?', 최근 '+c.last:'');
  const bb=$('bleBadge');bb.textContent=b.badge;bb.className='badge '+(b.badge=='OK'?'ok':b.badge=='PAIR'?'warn':'bad');
  let info=b.link=='connected'?'연결 '+dur(b.connectedMs)+', RSSI '+b.rssi+' dBm':b.link=='disconnected'&&b.retryMs>0?'재연결 '+Math.ceil(b.retryMs/1000)+'초 후':b.link;
  info+=', '+(b.paired?'페어링됨':'페어링 안 됨')+', 연결 '+b.connects+'회, 끊김 '+b.drops+'회, '+b.addr;
  $('bleInfo').textContent=info;
  $('wifiInfo').textContent=s.wifi.ip+' ('+s.wifi.rssi+' dBm)';
  $('pairHint').hidden=!b.pairing;
  $('sys').textContent='가동 '+dur(s.uptimeMs)+', 메모리 '+Math.round(s.heap/1024)+' KB'+(s.ntp?', NTP '+s.ntp:'');
}
async function poll(){
  try{render(await api('/api/status'));
    if($('logBox').open){const l=$('log'),atEnd=l.scrollTop+l.clientHeight>=l.scrollHeight-20;
      l.textContent=await (await fetch('/api/log',{cache:'no-store'})).text();if(atEnd)l.scrollTop=l.scrollHeight}}
  catch(e){$('clock').textContent='오프라인'}
}
function soon(){clearTimeout(pollTimer);pollTimer=setTimeout(poll,700)}
async function post(path,params,msg){
  try{render(await api(path,params));if(msg)toast(msg);soon()}catch(e){toast('실패: '+e.message)}
}
$('power').onclick=()=>post('/api/power',{state:st&&st.ble.power==1?'off':'on'});
function sendBri(v){briHold=Date.now()+2500;$('briVal').textContent=v+'%';post('/api/brightness',{value:v})}
$('bri').oninput=e=>{briHold=Date.now()+2500;$('briVal').textContent=e.target.value+'%';clearTimeout(briTimer);briTimer=setTimeout(()=>sendBri(e.target.value),250)};
document.querySelectorAll('.presets button').forEach(x=>x.onclick=()=>{$('bri').value=x.dataset.b;sendBri(x.dataset.b)});
['schOn','schOff','schBri'].forEach(id=>$(id).oninput=()=>schDirty=true);
function saveSch(){
  if(!$('schOn').value||!$('schOff').value){toast('시각을 입력하세요');return}
  if($('schOn').value==$('schOff').value){toast('ON과 OFF 시각이 같습니다');return}
  schDirty=false;
  post('/api/schedule',{enabled:$('schEn').checked?1:0,on:$('schOn').value,off:$('schOff').value,bri:$('schBri').value||0},'스케줄 저장됨');
}
$('schSave').onclick=saveSch;
$('schEn').onchange=saveSch;
$('pair').onclick=()=>{if(confirm('페어링을 시작합니다. 램프가 깜빡이면 90초 안에 램프 링을 돌려야 합니다.'))post('/api/pair',{},'페어링 요청 전송')};
$('refresh').onclick=()=>post('/api/refresh',{},'상태 요청 전송');
$('reboot').onclick=()=>{if(confirm('장치를 재부팅할까요?'))post('/api/reboot',{},'재부팅 중')};
$('logBox').ontoggle=poll;
poll();setInterval(poll,2000);
</script></body></html>
)HTML";
