#pragma once
#include <pgmspace.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Repeater</title>
<style>
:root{color-scheme:light dark;--bg:#d6dad4;--panel:#e9ece6;--field:#f5f6f2;--ink:#151815;--mute:#59615a;--line:#b5bbb3;--btn:#151815;--btn-ink:#f2f4ef;--ok:#2b7a4b;--warn:#a9680f;--bad:#b3261e;--scr:#05080b}
@media(prefers-color-scheme:dark){:root{--bg:#171b18;--panel:#212722;--field:#171b18;--ink:#e9ede7;--mute:#97a199;--line:#38413a;--btn:#e9ede7;--btn-ink:#151815;--ok:#6fd49b;--warn:#e7b25a;--bad:#ff8a80}}
*{box-sizing:border-box}
[hidden]{display:none!important}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.5 ui-sans-serif,system-ui,-apple-system,"Segoe UI",Roboto,"Helvetica Neue",sans-serif;font-variant-numeric:tabular-nums}
.wrap{max-width:560px;margin:0 auto;padding:0 16px 72px}
header{padding:24px 0 14px}
h1{margin:0;font-size:24px;font-weight:700;letter-spacing:-.02em}
#line{margin:3px 0 0;color:var(--mute);font-size:14px}
#line i{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--mute);margin-right:8px}
#line.ok i{background:var(--ok)}#line.bad i{background:var(--bad)}#line.busy i{background:var(--warn);animation:b 1s infinite}
@keyframes b{50%{opacity:.25}}
@media(prefers-reduced-motion:reduce){#line.busy i{animation:none}}
nav{display:flex;gap:26px;border-bottom:1px solid var(--line);overflow-x:auto}
nav button{background:none;border:0;border-bottom:2px solid transparent;margin-bottom:-1px;padding:10px 0;color:var(--mute);font:inherit;cursor:pointer;white-space:nowrap}
nav button.on{color:var(--ink);border-color:var(--ink);font-weight:600}
section{display:none;padding-top:20px}section.on{display:block}
h2{margin:30px 0 10px;font-size:14px;font-weight:700}
section>h2:first-child{margin-top:0}
.sub{margin:-4px 0 12px;font-size:13px;color:var(--mute)}
.group{background:var(--panel);border:1px solid var(--line);border-radius:6px;margin:0;padding:0;list-style:none}
.gr{display:flex;justify-content:space-between;align-items:center;gap:16px;padding:12px 14px;border-top:1px solid var(--line)}
.gr:first-child{border-top:0}
.gr small{display:block;color:var(--mute);font-size:13px}
.gr .mono{color:var(--mute);font-size:13px}
dl.group{display:grid;grid-template-columns:auto 1fr}
dl.group dt,dl.group dd{margin:0;padding:10px 14px;border-top:1px solid var(--line)}
dl.group dt:first-of-type,dl.group dd:first-of-type{border-top:0}
dl.group dt{color:var(--mute)}dl.group dd{text-align:right;word-break:break-all}
.mirror{position:relative;background:var(--scr);border-radius:8px;padding:14px;margin-bottom:10px}
.mirror canvas{display:block;width:100%;image-rendering:pixelated;aspect-ratio:2/1}
.mirror.off canvas{opacity:.25}
.mirror span{position:absolute;inset:0;display:none;align-items:center;justify-content:center;color:#7d8f9e;font-size:13px}
.mirror.off span{display:flex}
.tiles{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-bottom:22px}
.tile{background:var(--scr);border-radius:8px;padding:14px 12px 12px;min-width:0}
.tile canvas{display:block;height:28px;max-width:100%;image-rendering:pixelated}
.tl{margin-top:12px;font-size:13px;font-weight:600;color:#e4eef6}
.ts{font-size:12px;color:#8aa0b2;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.chart{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:6px 8px}
.chart canvas{display:block;width:100%;height:128px}
label{display:block;margin:0 0 16px;font-size:13px;color:var(--mute)}
input,select{width:100%;margin-top:6px;padding:10px 11px;background:var(--field);color:var(--ink);border:1px solid var(--line);border-radius:4px;font:inherit;outline:0}
input:focus-visible,select:focus-visible,button:focus-visible{outline:2px solid var(--ink);outline-offset:1px}
input[type=range]{padding:0;height:30px;accent-color:var(--ink);border:0;background:none}
input[type=checkbox]{width:auto;margin:0}
.chk{display:flex;align-items:center;gap:10px;color:var(--ink);font-size:15px}
.inp{display:flex;gap:8px;margin-top:6px}.inp input{margin:0}
.btn{font:inherit;font-weight:600;padding:10px 16px;border-radius:4px;border:1px solid var(--line);background:var(--panel);color:var(--ink);cursor:pointer}
.btn.pri{background:var(--btn);border-color:var(--btn);color:var(--btn-ink)}
.btn.dan{color:var(--bad)}.btn:disabled{opacity:.5;cursor:default}
.row{display:flex;gap:12px;align-items:center;flex-wrap:wrap}.between{justify-content:space-between}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:0 10px}
@media(max-width:460px){.grid{grid-template-columns:repeat(2,1fr)}}
.list li{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:12px 14px;border-top:1px solid var(--line);cursor:pointer}
.list li:first-child{border-top:0}.list li:hover{background:var(--bg)}
.list .meta{display:flex;gap:10px;align-items:center;color:var(--mute);font-size:13px;flex:none}
.list .name{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.list li.empty{cursor:default;color:var(--mute)}
.sw{position:relative;display:block;width:46px;height:26px;margin:0;flex:none}
.sw input{position:absolute;inset:0;width:100%;height:100%;margin:0;opacity:0;cursor:pointer;z-index:1}
.sw span{position:absolute;inset:0;background:var(--line);border-radius:13px;transition:background .15s}
.sw span:after{content:"";position:absolute;top:3px;left:3px;width:20px;height:20px;border-radius:50%;background:var(--panel);transition:transform .15s}
.sw input:checked+span{background:var(--btn)}.sw input:checked+span:after{transform:translateX(20px);background:var(--btn-ink)}
.sw input:focus-visible+span{outline:2px solid var(--ink);outline-offset:2px}
p.hint{margin:8px 0 0;font-size:13px;color:var(--mute)}
.out{float:right;color:var(--ink)}
#toast{position:fixed;left:50%;bottom:22px;transform:translate(-50%,16px);max-width:90vw;padding:10px 16px;border-radius:4px;background:var(--ink);color:var(--bg);font-size:14px;opacity:0;pointer-events:none;transition:opacity .2s,transform .2s}
#toast.show{opacity:1;transform:translate(-50%,0)}#toast.bad{background:var(--bad);color:#fff}
</style></head><body><div class="wrap">
<header><h1>Repeater</h1><p id="line"><i></i><span>Waiting for device</span></p></header>
<nav>
<button data-t="over" class="on">Overview</button>
<button data-t="conn">Connect</button>
<button data-t="ap">Access point</button>
<button data-t="dev">Device</button>
</nav>

<section id="over" class="on">
<div class="mirror" id="mirror"><canvas id="oled" width="128" height="64"></canvas><span>Screen is off</span></div>
<div class="tiles">
<div class="tile"><canvas id="v_sig"></canvas><div class="tl">Signal</div><div class="ts" id="s_sig">-</div></div>
<div class="tile"><canvas id="v_bat"></canvas><div class="tl">Battery</div><div class="ts" id="s_bat">-</div></div>
<div class="tile"><canvas id="v_cli"></canvas><div class="tl">Clients</div><div class="ts" id="s_cli">-</div></div>
</div>
<ul class="group"><li class="gr"><div>Repeater<small>Forward traffic between the upstream network and the access point</small></div>
<label class="sw"><input type="checkbox" id="rep" aria-label="Repeater"><span></span></label></li></ul>
<ul class="group" style="margin-top:8px"><li class="gr"><div>Internet<small id="inetTxt">Checking...</small></div><button class="btn" id="speedBtn">Speed test</button></li></ul>
<h2>Signal, last 3 minutes</h2>
<div class="chart"><canvas id="hist"></canvas></div>
<h2>Connected devices</h2>
<ul class="group" id="cl"><li class="gr"><span class="mono">None</span></li></ul>
<h2>Details</h2>
<dl class="group" id="kv"></dl>
</section>

<section id="conn">
<h2>Saved networks</h2>
<ul class="group list" id="saved"><li class="empty">None yet. Networks you join are remembered here.</li></ul>
<div class="row between" style="margin-top:30px"><h2 style="margin:0">Nearby networks</h2><button class="btn" id="scan">Scan</button></div>
<ul class="group list" id="nets" style="margin-top:12px"><li class="empty">Press Scan to look for networks.</li></ul>
<h2>Join a network</h2>
<label>Network name<input id="ssid" maxlength="32" autocomplete="off" autocapitalize="none" spellcheck="false"></label>
<label>Password<div class="inp"><input id="pass" type="password" maxlength="63" autocomplete="off" autocapitalize="none" spellcheck="false" placeholder="Leave empty for an open network"><button type="button" class="btn" id="eye">Show</button></div></label>
<div class="row"><button class="btn pri" id="join">Connect</button><span id="cmsg" class="hint" style="margin:0"></span></div>
</section>

<section id="ap">
<label>Network name<input id="apSsid" maxlength="32" autocomplete="off" spellcheck="false"></label>
<label>Password<input id="apPass" maxlength="63" autocomplete="off" autocapitalize="none" spellcheck="false">
<p class="hint">8 to 63 characters. Leave empty to make the network open.</p></label>
<label>Maximum clients<select id="maxc"></select></label>
<label>DNS for connected devices<select id="dnsMode"><option value="0">Same as upstream (auto)</option><option value="1">Cloudflare 1.1.1.1</option><option value="2">Google 8.8.8.8</option><option value="3">Quad9 9.9.9.9</option><option value="4">AdGuard 94.140.14.14 (blocks ads)</option><option value="5">Custom</option></select>
<p class="hint">Devices pick up a changed DNS when they reconnect or renew their address.</p></label>
<label id="dnsCustomWrap" hidden>Custom DNS address<input id="dnsCustom" inputmode="decimal" placeholder="e.g. 192.168.0.2" autocomplete="off" spellcheck="false"></label>
<label class="chk"><input type="checkbox" id="apHidden">Hide network name</label>
<label class="chk" style="margin-top:12px"><input type="checkbox" id="apSetupOpen">Keep the access point open until connected</label>
<p class="hint">While there is no upstream connection (at startup, or after the link is lost for a while) the access point has no password so you can reach this page. Once connected it switches to the password above and nearby clients must reconnect.</p>
<div class="row" style="margin-top:20px"><button class="btn pri" id="saveAp">Save changes</button></div>
<p class="hint">Saving restarts the access point. Reconnect to it afterwards.</p>
</section>

<section id="dev">
<h2>Display</h2>
<label>Brightness <span class="out" id="briv"></span><input type="range" id="bri" min="5" max="100" step="5"></label>
<label>Turn screen off after<select id="tmo">
<option value="0">Never</option><option value="15">15 seconds</option><option value="30">30 seconds</option>
<option value="60">1 minute</option><option value="120">2 minutes</option><option value="300">5 minutes</option></select></label>
<label class="chk"><input type="checkbox" id="flip">Rotate screen 180 degrees</label>

<h2>Status LED</h2>
<p class="sub">NeoPixel on the LED data pin.</p>
<label>Shows<select id="ledMode"><option value="0">Nothing (off)</option><option value="1">Connection status</option><option value="2">Battery level</option><option value="3">Alternate: connection, then battery</option></select></label>
<label>Brightness <span class="out" id="ledv"></span><input type="range" id="ledBright" min="5" max="100" step="5"></label>
<label>Number of pixels<select id="ledCount"></select></label>
<p class="hint">Connection: green connected, blue connected with repeater off, amber pulse connecting, red failed or lost, violet pulse not set up. Battery: green, amber, orange, flashing red below 15%.</p>
<p class="hint" id="ledNow"></p>

<h2>Battery</h2>
<ul class="group" style="margin-bottom:16px"><li class="gr"><span>Reading</span><span id="batRead">-</span></li></ul>
<label>Voltage that reads as full scale on A0 (mV)<input id="adcFullMv" type="number" min="1000" max="20000" inputmode="numeric"></label>
<label>Calibrate from a multimeter reading (V)<div class="inp"><input id="calV" type="number" step="0.01" min="2" max="30" inputmode="decimal" placeholder="e.g. 3.92"><button type="button" class="btn" id="calBtn">Calibrate</button></div>
<p class="hint">Measure the battery voltage, type it here and press Calibrate. Do it with the battery connected and the device on.</p></label>

<h2>Pins</h2>
<div class="grid" id="pins"></div>
<p class="hint">GPIO numbers, not board labels (D1 mini: D1=5, D2=4, D3=0, D4=2, D5=14, D6=12, D7=13, D8=15, RX=3). Use -1 to disable. Buttons connect the pin to GND. Pin changes restart the device.</p>
<div class="row" style="margin-top:20px"><button class="btn pri" id="saveDev">Save changes</button></div>

<h2>System</h2>
<div class="row"><button class="btn" id="reboot">Restart</button><button class="btn dan" id="factory">Reset settings</button></div>
<p class="hint" id="ver"></p>
</section>
</div><div id="toast" role="status"></div>
<script>
const $=s=>document.querySelector(s),$$=s=>[...document.querySelectorAll(s)];
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function api(p,b){const o=b?{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(b)}:{};
const r=await fetch(p,o);let j={};try{j=await r.json()}catch(e){}if(!r.ok||j.ok===false)throw new Error(j.error||'Request failed');return j}
let tm;function toast(m,bad){const t=$('#toast');t.textContent=m;t.className='show'+(bad?' bad':'');clearTimeout(tm);tm=setTimeout(()=>t.className='',3200)}
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const bars=r=>{const n=r>=-55?4:r>=-65?3:r>=-75?2:r>=-85?1:0;let s='<svg width="16" height="12" viewBox="0 0 16 12" aria-hidden="true">';
for(let i=0;i<4;i++){const h=3+i*3;s+='<rect x="'+i*4+'" y="'+(12-h)+'" width="3" height="'+h+'" fill="currentColor" opacity="'+(i<n?1:.25)+'"/>'}return s+'</svg>'};
const upt=s=>{const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return d?d+'d '+h+'h '+m+'m':h?h+'h '+m+'m':m+'m '+s%60+'s'};
const qual=r=>r>=-55?'Excellent':r>=-65?'Good':r>=-75?'Fair':'Weak';

$$('nav button').forEach(b=>b.onclick=()=>{$$('nav button').forEach(x=>x.classList.toggle('on',x===b));
$$('section').forEach(s=>s.classList.toggle('on',s.id===b.dataset.t));
if(b.dataset.t==='ap'||b.dataset.t==='dev')loadCfg();if(b.dataset.t==='conn')loadSaved();if(b.dataset.t==='over'){mirror();drawHist()}});

/* dot-matrix readouts, same 5x7 grid as the OLED */
const G={'0':[62,81,73,69,62],'1':[0,66,127,64,0],'2':[66,97,81,73,70],'3':[33,65,69,75,49],'4':[24,20,18,127,16],'5':[39,69,69,69,57],'6':[60,74,73,73,48],'7':[1,113,9,5,3],'8':[54,73,73,73,54],'9':[6,73,73,41,30],
'-':[8,8,8,8,8],'.':[0,96,96,0,0],'%':[35,19,8,100,98],'U':[63,64,64,64,63],'S':[70,73,73,73,49],'B':[127,73,73,73,54],' ':[0,0,0,0,0]};
function dots(cv,str,px){const n=str.length,w=(n*6-1)*px;cv.width=w;cv.height=7*px;cv.style.width=w+'px';const x=cv.getContext('2d');x.clearRect(0,0,w,7*px);x.fillStyle='#cfe9ff';
for(let i=0;i<n;i++){const g=G[str[i]]||G[' '];for(let c=0;c<5;c++)for(let r=0;r<7;r++)if(g[c]>>r&1)x.fillRect((i*6+c)*px,r*px,px-1,px-1)}}

/* live copy of the OLED */
const cx=$('#oled').getContext('2d'),img=cx.createImageData(128,64);
async function mirror(){if(!$('#over').classList.contains('on'))return;
try{const j=await api('/api/screen');$('#mirror').classList.toggle('off',!j.on);if(j.buf.length!==2048)return;
for(let p=0;p<8;p++)for(let x=0;x<128;x++){const v=parseInt(j.buf.substr((p*128+x)*2,2),16);
for(let k=0;k<8;k++){const o=((p*8+k)*128+x)*4,on=(v>>k)&1;img.data[o]=on?207:4;img.data[o+1]=on?233:8;img.data[o+2]=on?255:12;img.data[o+3]=255}}
cx.putImageData(img,0,0)}catch(e){}}

/* signal history */
const hist=[],HN=90;
function drawHist(){const cv=$('#hist'),d=window.devicePixelRatio||1,w=cv.clientWidth,h=cv.clientHeight;if(!w)return;
cv.width=w*d;cv.height=h*d;const x=cv.getContext('2d');x.scale(d,d);
const st=getComputedStyle(document.documentElement),ink=st.getPropertyValue('--ink').trim(),mute=st.getPropertyValue('--mute').trim(),line=st.getPropertyValue('--line').trim();
const lo=-95,hi=-30,Y=v=>8+(hi-v)/(hi-lo)*(h-16);
x.font='11px system-ui,sans-serif';x.lineWidth=1;
[-50,-70,-90].forEach(v=>{x.strokeStyle=line;x.beginPath();x.moveTo(34,Y(v)+.5);x.lineTo(w,Y(v)+.5);x.stroke();x.fillStyle=mute;x.fillText(v,2,Y(v)+4)});
const step=(w-40)/(HN-1);x.strokeStyle=ink;x.lineWidth=2;x.lineJoin='round';x.beginPath();let pen=false;
hist.forEach((v,i)=>{if(v===null){pen=false;return}const px=34+(HN-hist.length+i)*step,py=Y(Math.max(lo,Math.min(hi,v)));if(pen)x.lineTo(px,py);else x.moveTo(px,py);pen=true});x.stroke()}

/* status */
async function refresh(){const ln=$('#line');
try{const s=await api('/api/status');
ln.className={connected:'ok',connecting:'busy',failed:'bad'}[s.state]||'';
ln.lastChild.textContent=s.state==='connected'?'Connected to '+s.ssid:s.state==='connecting'?'Connecting to '+s.ssid:s.state==='failed'?(s.reason||'Connection failed'):'Not connected';
if(document.activeElement!==$('#rep'))$('#rep').checked=s.repeater;
const c=s.state==='connected';
dots($('#v_sig'),c?String(s.rssi):'--',4);$('#s_sig').textContent=c?'dBm, '+qual(s.rssi).toLowerCase()+', ch '+s.channel:'No uplink';
dots($('#v_bat'),s.batPresent?s.batPct+'%':'USB',4);$('#s_bat').textContent=s.batPresent?(s.batMv/1000).toFixed(2)+' V':'No battery';
dots($('#v_cli'),String(s.clients),4);$('#s_cli').textContent=s.repeater?'Repeater on':'Repeater off';
hist.push(c?s.rssi:null);if(hist.length>HN)hist.shift();drawHist();
const kv=[['Upstream network',s.ssid||'None'],['Upstream address',c?s.ip:'-'],['Gateway',c?s.gw:'-'],['DNS for devices',s.dnsName+', '+s.dnsIp],['Access point',s.apSsid+(s.apOpen?' (open, setup mode)':'')],['Web UI address',s.apIp],['Free memory',(s.heap/1024).toFixed(1)+' KB'],['Uptime',upt(s.uptime)]];
$('#kv').innerHTML=kv.map(r=>'<dt>'+r[0]+'</dt><dd>'+esc(r[1])+'</dd>').join('');
$('#ver').textContent='Firmware '+s.fw;
$('#ledNow').textContent='Showing now: '+s.led;
const it=s.inet==='online'?'Online, '+s.ping+' ms, DNS '+s.dnsMs+' ms':s.inet==='nodns'?'Reachable, but DNS is not answering':s.inet==='offline'?'No internet':c?'Checking...':'Not connected';
const sp=s.speedState===1?' | Testing...':s.speedState===2?' | '+(s.speedKbps/1000).toFixed(1)+' Mbps down':s.speedState===3?' | Speed test failed':'';
$('#inetTxt').textContent=it+sp;$('#speedBtn').disabled=!c||s.speedState===1;
$('#batRead').textContent=s.batPresent?(s.batMv/1000).toFixed(2)+' V, '+s.batPct+'%, raw '+s.adcRaw:'No battery detected, raw '+s.adcRaw;
if($('#over').classList.contains('on')){const k=await api('/api/clients');
$('#cl').innerHTML=k.list.length?k.list.map(d=>'<li class="gr"><span>'+esc(d.ip)+'</span><span class="mono">'+esc(d.mac)+'</span></li>').join(''):'<li class="gr"><span class="mono">No devices connected</span></li>'}
}catch(e){ln.className='bad';ln.lastChild.textContent='Device not reachable'}}

/* connect */
let nets=[],busy=false;
function drawNets(){const ul=$('#nets');
if(!nets.length){ul.innerHTML='<li class="empty">No networks found. Try scanning again.</li>';return}
ul.innerHTML=nets.map((n,i)=>'<li data-i="'+i+'"><span class="name">'+esc(n.ssid)+'</span><span class="meta">'+(savedSet.has(n.ssid)?'Saved':(n.enc?'Secured':'Open'))+'<span>'+n.rssi+' dBm</span>'+bars(n.rssi)+'</span></li>').join('');
$$('#nets li[data-i]').forEach(li=>li.onclick=()=>{const n=nets[li.dataset.i];$('#ssid').value=n.ssid;$('#pass').value='';$('#pass').placeholder=savedSet.has(n.ssid)?'Saved password will be used':'Leave empty for an open network';(n.enc&&!savedSet.has(n.ssid)?$('#pass'):$('#join')).focus()})}
async function scan(){if(busy)return;busy=true;$('#scan').disabled=true;$('#nets').innerHTML='<li class="empty">Scanning...</li>';
try{await api('/api/scan',{});for(let i=0;i<25;i++){await sleep(900);const j=await api('/api/networks');if(!j.scanning){nets=j.list;break}}}catch(e){toast(e.message,1)}
busy=false;$('#scan').disabled=false;drawNets()}
$('#scan').onclick=scan;
$('#eye').onclick=()=>{const p=$('#pass'),h=p.type==='password';p.type=h?'text':'password';$('#eye').textContent=h?'Hide':'Show'};
$('#join').onclick=async()=>{const ssid=$('#ssid').value.trim(),pass=$('#pass').value;
if(!ssid)return toast('Enter a network name',1);if(pass&&pass.length<8)return toast('Password must be at least 8 characters',1);
const m=$('#cmsg');try{await api('/api/connect',{ssid:ssid,pass:pass});m.textContent='Connecting...';
let done=false;for(let i=0;i<32&&!done;i++){await sleep(1000);try{const s=await api('/api/status');
if(s.state!=='connecting'){done=true;m.textContent=s.state==='connected'?'Connected to '+s.ssid:(s.reason||'Connection failed')}}catch(e){}}
loadSaved();if(!done)m.textContent='No answer yet. If this page stops responding, reconnect to the repeater.'}catch(e){m.textContent='';toast(e.message,1)}};
let savedSet=new Set(),savedList=[];
async function loadSaved(){try{const j=await api('/api/saved');savedList=j.list;savedSet=new Set(j.list.map(x=>x.ssid));
$('#saved').innerHTML=j.list.length?j.list.map((x,i)=>'<li><span class="name">'+esc(x.ssid)+(x.current?' <small>(connected)</small>':'')+'</span><span class="row" style="gap:8px;flex:none"><button class="btn" style="padding:6px 12px" data-j="'+i+'">'+(x.current?'Reconnect':'Join')+'</button><button class="btn dan" style="padding:6px 12px" data-f="'+i+'">Forget</button></span></li>').join(''):'<li class="empty">None yet. Networks you join are remembered here.</li>';
if(nets.length)drawNets()}catch(e){}}
$('#saved').onclick=async e=>{const b=e.target.closest('button');if(!b)return;
try{if(b.dataset.j!==undefined){const x=savedList[b.dataset.j];await api('/api/connect',{ssid:x.ssid,pass:''});toast('Connecting to '+x.ssid)}
else if(b.dataset.f!==undefined){const x=savedList[b.dataset.f];if(!confirm('Forget '+x.ssid+'?'))return;await api('/api/forget',{ssid:x.ssid});toast('Network forgotten');loadSaved()}}catch(x){toast(x.message,1)}};
$('#speedBtn').onclick=async()=>{try{await api('/api/speedtest',{});toast('Measuring download speed')}catch(e){toast(e.message,1)}};
$('#dnsMode').onchange=()=>{$('#dnsCustomWrap').hidden=$('#dnsMode').value!=='5'};
$('#rep').onchange=async e=>{try{await api('/api/repeater',{on:e.target.checked?1:0})}catch(x){toast(x.message,1);e.target.checked=!e.target.checked}};

/* settings */
const PINS=[['up','Up'],['down','Down'],['left','Left'],['right','Right'],['ok','OK'],['back','Back'],['sda','OLED SDA'],['scl','OLED SCL'],['led','LED data']];
$('#pins').innerHTML=PINS.map(k=>'<label>'+k[1]+'<input id="p_'+k[0]+'" type="number" min="-1" max="16" inputmode="numeric"></label>').join('');
$('#maxc').innerHTML=[1,2,3,4,5,6,7,8].map(n=>'<option>'+n+'</option>').join('');
$('#ledCount').innerHTML=[1,2,3,4,5,6,7,8].map(n=>'<option>'+n+'</option>').join('');
$('#bri').oninput=()=>$('#briv').textContent=$('#bri').value+'%';
$('#ledBright').oninput=()=>$('#ledv').textContent=$('#ledBright').value+'%';
async function loadCfg(){try{const c=await api('/api/config');
$('#apSsid').value=c.apSsid;$('#apPass').value=c.apPass;$('#apHidden').checked=!!c.apHidden;$('#apSetupOpen').checked=!!c.apSetupOpen;$('#maxc').value=c.maxClients;
$('#bri').value=c.brightness;$('#briv').textContent=c.brightness+'%';$('#tmo').value=c.timeout;$('#flip').checked=!!c.flip;
$('#dnsMode').value=c.dnsMode;$('#dnsCustom').value=c.dnsCustom==='0.0.0.0'?'':c.dnsCustom;$('#dnsCustomWrap').hidden=c.dnsMode!==5;$('#ledMode').value=c.ledMode;$('#ledBright').value=c.ledBright;$('#ledv').textContent=c.ledBright+'%';$('#ledCount').value=c.ledCount;$('#adcFullMv').value=c.adcFullMv;
PINS.forEach(k=>$('#p_'+k[0]).value=c.pins[k[0]])}catch(e){toast(e.message,1)}}
const pk=k=>'p'+k[0].toUpperCase()+k.slice(1);
$('#saveAp').onclick=async()=>{try{const r=await api('/api/config',{apSsid:$('#apSsid').value,apPass:$('#apPass').value,apHidden:$('#apHidden').checked?1:0,apSetupOpen:$('#apSetupOpen').checked?1:0,maxClients:$('#maxc').value,dnsMode:$('#dnsMode').value,dnsCustom:$('#dnsCustom').value});
toast(r.apRestart?'Saved. The access point is restarting.':'Saved')}catch(e){toast(e.message,1)}};
$('#saveDev').onclick=async()=>{const b={brightness:$('#bri').value,timeout:$('#tmo').value,flip:$('#flip').checked?1:0,
ledMode:$('#ledMode').value,ledBright:$('#ledBright').value,ledCount:$('#ledCount').value,adcFullMv:$('#adcFullMv').value};
PINS.forEach(k=>{const v=$('#p_'+k[0]).value;b[pk(k[0])]=v===''?-1:v});
try{const r=await api('/api/config',b);toast(r.restart?'Saved. Restarting to apply pins.':'Saved')}catch(e){toast(e.message,1)}};
$('#calBtn').onclick=async()=>{const v=parseFloat($('#calV').value);if(!(v>=2&&v<=30))return toast('Enter the measured voltage in volts',1);
try{await api('/api/battery/cal',{mv:Math.round(v*1000)});toast('Calibrated');$('#calV').value='';loadCfg()}catch(e){toast(e.message,1)}};
$('#reboot').onclick=async()=>{if(!confirm('Restart the device?'))return;try{await api('/api/reboot',{})}catch(e){}toast('Restarting')};
$('#factory').onclick=async()=>{if(!confirm('Erase all settings, including the saved network?'))return;try{await api('/api/factory',{})}catch(e){}toast('Settings erased. Restarting')};

window.addEventListener('resize',drawHist);
loadSaved();refresh();mirror();setInterval(refresh,2000);setInterval(mirror,1000);
</script></body></html>
)HTML";
