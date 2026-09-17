#pragma once

#include <Arduino.h>

// Single-page UI: login, LED controls, effect script builder, Wi-Fi, MQTT and security settings.
// All API calls send X-DeskLED so a 401 shows the login form instead of the browser's password dialog.
const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DeskLED</title>
<style>
:root{--bg:#111316;--card:#1b1e23;--line:#2a2e35;--fg:#e8eaed;--dim:#8a919c;--acc:#ff9a3c;--bad:#ff6b6b}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;padding:16px}
main{max-width:520px;margin:0 auto}
h1{font-size:20px;margin:4px 0 16px;display:flex;align-items:center;gap:10px}
h1 button{margin-left:auto;font-size:13px;padding:6px 10px}
#swatch{width:18px;height:18px;border-radius:50%;background:#000;box-shadow:0 0 12px #000;transition:background .2s,box-shadow .2s}
section{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:12px}
h2{font-size:13px;text-transform:uppercase;letter-spacing:.06em;color:var(--dim);margin:0 0 10px;font-weight:600}
.row{display:flex;align-items:center;gap:12px;margin:10px 0}
.row label{width:90px;color:var(--dim);flex:none}
.row output{width:44px;text-align:right;font-variant-numeric:tabular-nums;flex:none}
input[type=range]{flex:1;accent-color:var(--acc);min-width:0}
input[type=color]{width:100%;height:48px;border:0;border-radius:8px;background:none;padding:0;cursor:pointer}
input[type=text],input[type=password],select,textarea{width:100%;padding:10px;border-radius:8px;border:1px solid var(--line);background:var(--bg);color:var(--fg);font:inherit}
textarea{font:13px/1.4 ui-monospace,Menlo,Consolas,monospace;resize:vertical;min-height:40px}
button{font:inherit;color:var(--fg);background:var(--bg);border:1px solid var(--line);border-radius:8px;padding:9px 12px;cursor:pointer}
button.on{border-color:var(--acc);color:var(--acc)}
button.primary{background:var(--acc);border-color:var(--acc);color:#111;font-weight:600}
button.danger{border-color:var(--bad);color:var(--bad)}
.modes{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.modes button{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.btns{display:flex;flex-wrap:wrap;gap:8px}
#power{width:100%;padding:12px;font-size:16px}
.nets{max-height:180px;overflow:auto;margin:8px 0}
.nets button{display:flex;justify-content:space-between;width:100%;margin-bottom:6px;text-align:left}
.stack>*{margin-bottom:8px}
small,.dim{color:var(--dim)}
.err{color:var(--bad)}
.lbl{display:block;color:var(--dim);font-size:13px;margin-bottom:4px}
#pv{width:100%;height:56px;border-radius:8px;display:block;background:#000}
#pvSwatch{height:40px;border-radius:8px;background:#000;margin-bottom:6px}
code{font-size:12px;background:var(--bg);padding:1px 4px;border-radius:4px}
details>summary{cursor:pointer}
#login{max-width:340px;margin:12vh auto 0}
[hidden]{display:none!important}
</style></head><body>

<section id="login" hidden>
  <h2>DeskLED login</h2>
  <div class="stack">
    <input type="password" id="lPass" placeholder="Admin password" autocomplete="current-password">
    <button class="primary" id="lGo">Log in</button>
    <div id="lMsg" class="err"></div>
  </div>
</section>

<main id="app" hidden>
<h1><span id="swatch"></span>DeskLED<button id="logout">Log out</button></h1>
<section id="pwWarn" hidden style="border-color:var(--acc);cursor:pointer"><b>Default password in use.</b> <span class="dim">Tap here to change it.</span></section>

<section>
  <button id="power">Power</button>
  <div class="row"><label>Brightness</label><input type="range" id="brightness" min="1" max="255"><output id="brightnessV"></output></div>
</section>

<section>
  <h2>Effect</h2>
  <div class="modes" id="modes"></div>
  <div class="row"><label>Speed</label><input type="range" id="speed" min="1" max="255"><output id="speedV"></output></div>
  <div class="row"><label>Transition</label><input type="range" id="transition" min="0" max="3000" step="50"><output id="transitionV"></output></div>
  <div id="tempInfo" class="dim" hidden></div>
</section>

<section>
  <h2>Color</h2>
  <input type="color" id="color">
  <small>Used by solid, breathe, candle, strobe, and scripts via cr, cg, cb.</small>
</section>

<section>
  <h2>Script builder</h2>
  <div class="stack">
    <div class="btns"><select id="sPick" style="flex:1"></select><button id="sNew">New</button></div>
    <input type="text" id="sName" placeholder="name (a-z 0-9 - _)" autocomplete="off">
    <select id="sSpace"><option value="hsv">HSV: hue (degrees), saturation 0-1, value 0-1</option><option value="rgb">RGB: red, green, blue 0-1</option></select>
    <div><span class="lbl" id="aL">Hue</span><textarea id="sA" rows="2" spellcheck="false"></textarea></div>
    <div><span class="lbl" id="bL">Saturation</span><textarea id="sB" rows="1" spellcheck="false"></textarea></div>
    <div><span class="lbl" id="cL">Value</span><textarea id="sC" rows="2" spellcheck="false"></textarea></div>
    <div><div id="pvSwatch"></div><canvas id="pv" width="480" height="56"></canvas>
      <small id="pvMsg">Live preview in the browser (last 5 seconds, newest on the right).</small></div>
    <div class="btns">
      <button id="sTry">Try on LEDs (15 s)</button>
      <button class="primary" id="sSave">Save</button>
      <button class="danger" id="sDel">Delete</button>
    </div>
    <div id="sMsg" class="dim"></div>
    <details><summary class="dim">Examples</summary><div class="btns" id="examples" style="margin-top:8px"></div></details>
    <details><summary class="dim">Reference</summary><small>
      <p><b>Variables:</b> <code>t</code> seconds since start, <code>speed</code> 1-255, <code>k</code> = speed/128,
      base color <code>cr cg cb</code> (0-1) and <code>ch cs cv</code> (hue degrees, 0-1).</p>
      <p><b>Functions:</b> <code>wave(x)</code> smooth 0-1-0 per unit, <code>tri(x)</code>, <code>sq(x)</code>, <code>pulse(x,duty)</code>,
      <code>frac(x)</code>, <code>noise(x)</code>, <code>rand()</code>, <code>smooth(x)</code>, <code>mix(a,b,f)</code>, <code>clamp(x,lo,hi)</code>,
      <code>sel(cond,a,b)</code>, <code>min max abs floor sin cos pow sqrt</code>.</p>
      <p><b>Operators:</b> <code>+ - * / % ^ &lt; &lt;= &gt; &gt;= == != &amp;&amp; || !</code>. Brightness is applied on top.</p>
    </small></details>
  </div>
</section>

<section>
  <h2>Wi-Fi</h2>
  <div id="wifiInfo" class="dim"></div>
  <details id="wifiBox"><summary class="dim">Change network</summary>
    <div class="stack" style="margin-top:10px">
      <button id="scan">Scan networks</button>
      <div class="nets" id="nets"></div>
      <input type="text" id="ssid" placeholder="Network name (SSID)" autocomplete="off">
      <input type="password" id="pass" placeholder="Password" autocomplete="off">
      <input type="text" id="host" placeholder="Hostname (optional)" autocomplete="off">
      <button class="primary" id="save">Save and connect</button>
      <div id="msg" class="dim"></div>
    </div>
  </details>
</section>

<section>
  <h2>Home Assistant (MQTT)</h2>
  <div id="mqttInfo" class="dim"></div>
  <details id="mqttBox"><summary class="dim">Broker settings</summary>
    <div class="stack" style="margin-top:10px">
      <input type="text" id="mHost" placeholder="Broker host or IP" autocomplete="off">
      <input type="text" id="mPort" placeholder="Port (1883)" inputmode="numeric" autocomplete="off">
      <input type="text" id="mUser" placeholder="Username (optional)" autocomplete="off">
      <input type="password" id="mPass" placeholder="Password (leave empty to keep)" autocomplete="off">
      <label class="dim"><input type="checkbox" id="mEn"> Enabled</label>
      <button class="primary" id="mSave">Save</button>
      <div id="mMsg" class="dim"></div>
    </div>
  </details>
</section>

<section>
  <h2>Security</h2>
  <details id="pwBox"><summary class="dim">Admin password</summary>
    <div class="stack" style="margin-top:10px">
      <input type="password" id="pwCur" placeholder="Current password" autocomplete="current-password">
      <input type="password" id="pwNew" placeholder="New password (8-63 characters)" autocomplete="new-password">
      <input type="password" id="pwNew2" placeholder="Repeat new password" autocomplete="new-password">
      <button class="primary" id="pwSave">Change admin password</button>
      <div id="pwMsg" class="dim"></div>
    </div>
  </details>
  <details id="tokBox"><summary class="dim">API tokens</summary>
    <div class="stack" style="margin-top:10px">
      <small>Give an app its own token instead of the admin password. A <b>control</b> token can only work the
      light; an <b>admin</b> token can change every setting. Tokens are shown once.</small>
      <div id="tokList" class="dim"></div>
      <input type="text" id="tokName" placeholder="What is it for? (e.g. home-assistant)" autocomplete="off">
      <select id="tokScope"><option value="control">control — light only</option><option value="admin">admin — full access</option></select>
      <button class="primary" id="tokAdd">Create token</button>
      <div id="tokNew" class="dim" style="word-break:break-all"></div>
    </div>
  </details>
  <details><summary class="dim">Setup hotspot password</summary>
    <div class="stack" style="margin-top:10px">
      <small>Protects the DeskLED-xxxx hotspot that appears when Wi-Fi fails.</small>
      <input type="password" id="apNew" placeholder="New hotspot password (8-63 characters)" autocomplete="new-password">
      <button class="primary" id="apSave">Change hotspot password</button>
      <div id="apMsg" class="dim"></div>
    </div>
  </details>
</section>

<section><h2>Device</h2><div id="dev" class="dim"></div>
  <p class="dim">Firmware update (signed images only): <a href="/update" style="color:var(--acc)">/update</a><br>API description: <a href="/openapi.json" style="color:var(--acc)">/openapi.json</a></p>
</section>
</main>

<script>
function md5(s){s=unescape(encodeURIComponent(s));const K=[],S=[7,12,17,22,5,9,14,20,4,11,16,23,6,10,15,21];for(let i=0;i<64;i++)K[i]=Math.floor(Math.abs(Math.sin(i+1))*4294967296)>>>0;const n=((s.length+8)>>6)+1,w=new Array(n*16).fill(0);for(let i=0;i<s.length;i++)w[i>>2]|=s.charCodeAt(i)<<(i%4*8);w[s.length>>2]|=128<<(s.length%4*8);w[n*16-2]=s.length*8;let a0=0x67452301,b0=0xefcdab89,c0=0x98badcfe,d0=0x10325476;for(let j=0;j<n*16;j+=16){let a=a0,b=b0,c=c0,d=d0;for(let i=0;i<64;i++){let f,g;if(i<16){f=(b&c)|(~b&d);g=i}else if(i<32){f=(d&b)|(~d&c);g=(5*i+1)%16}else if(i<48){f=b^c^d;g=(3*i+5)%16}else{f=c^(b|~d);g=7*i%16}const t=d;d=c;c=b;const x=(a+f+K[i]+w[j+g])|0,r=S[4*(i>>4)+i%4];b=(b+((x<<r)|(x>>>(32-r))))|0;a=t}a0=(a0+a)|0;b0=(b0+b)|0;c0=(c0+c)|0;d0=(d0+d)|0}let h='';for(const v of[a0,b0,c0,d0])for(let i=0;i<4;i++)h+=((v>>>(i*8))&255).toString(16).padStart(2,'0');return h}

const $=id=>document.getElementById(id);
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
let st={},timer=null,pending={},scriptList=[],started=false;
const hex=c=>'#'+[c.r,c.g,c.b].map(v=>v.toString(16).padStart(2,'0')).join('');

function api(path,method='GET',body){
  return fetch(path,{method,headers:Object.assign({'X-DeskLED':'1'},body?{'Content-Type':'application/json'}:{}),body:body?JSON.stringify(body):undefined})
    .then(r=>{
      if(r.status===401){showLogin();throw new Error('login required')}
      return r.json().catch(()=>({})).then(j=>{if(!r.ok)throw new Error(j.error||r.status);return j});
    });
}

// ---------- login ----------
function showLogin(){$('app').hidden=true;$('login').hidden=false;$('lPass').focus()}
function login(){
  const pw=$('lPass').value;$('lMsg').textContent='';
  api('/api/login').then(c=>api('/api/login','POST',{nonce:c.nonce,response:md5(md5(c.user+':'+c.realm+':'+pw)+':'+c.nonce)}))
    .then(()=>{$('lPass').value='';$('login').hidden=true;start()})
    .catch(e=>$('lMsg').textContent=e.message);
}
$('lGo').onclick=login;$('lPass').onkeydown=e=>{if(e.key==='Enter')login()};
$('logout').onclick=()=>api('/api/logout','POST').finally(()=>location.reload());

// ---------- controls ----------
function render(){
  $('power').textContent=st.on?'On':'Off';$('power').classList.toggle('on',st.on);
  for(const k of['brightness','speed','transition']){if(document.activeElement!==$(k))$(k).value=st[k];$(k+'V').value=k==='transition'?(st[k]/1000).toFixed(1)+'s':st[k];}
  if(document.activeElement!==$('color'))$('color').value=hex(st.color);
  document.querySelectorAll('#modes button').forEach(b=>b.classList.toggle('on',b.dataset.m===st.mode));
  const c=st.on?hex(st.color):'#000';
  $('swatch').style.background=st.on&&st.mode==='rgbfade'?'conic-gradient(red,yellow,lime,cyan,blue,magenta,red)':c;
  $('swatch').style.boxShadow='0 0 12px '+c;
  $('tempInfo').hidden=!st.temporary;
  if(st.temporary)$('tempInfo').textContent='Temporary effect, back to '+st.temporary.restore.mode+' in '+Math.ceil(st.temporary.remaining/1000)+' s';
}
function send(p,delay=120){
  Object.assign(pending,p);Object.assign(st,p);render();
  clearTimeout(timer);
  timer=setTimeout(()=>{const b=pending;pending={};api('/api/state','POST',b).then(s=>{st=s;render()}).catch(e=>console.warn(e))},delay);
}
$('power').onclick=()=>send({on:!st.on},0);
for(const k of['brightness','speed','transition'])$(k).oninput=e=>send({[k]:+e.target.value});
$('color').oninput=e=>{const v=e.target.value;send({color:{r:parseInt(v.substr(1,2),16),g:parseInt(v.substr(3,2),16),b:parseInt(v.substr(5,2),16)}})};

function loadModes(){
  return api('/api/modes').then(ms=>{
    $('modes').innerHTML='';
    ms.forEach(m=>{const b=document.createElement('button');b.textContent=m;b.title=m;b.dataset.m=m;b.onclick=()=>send({mode:m},0);$('modes').appendChild(b)});
    render();
  });
}
function loadState(){return api('/api/state').then(s=>{st=s;render()})}

// ---------- script builder ----------
const EXAMPLES={
  police:{space:'hsv',a:'sel(sq(t*k),0,240)',b:'1',c:'pulse(t*k*8,0.5)'},
  fire:{space:'hsv',a:'8+noise(t*k*5)*30',b:'1',c:'0.45+0.55*noise(t*k*9+50)'},
  ocean:{space:'hsv',a:'190+35*wave(t*k*0.15)',b:'0.85',c:'0.35+0.65*wave(t*k*0.4)'},
  heartbeat:{space:'rgb',a:'cr*max(pulse(t*k,0.08),0.6*pulse(t*k-0.2,0.08))',b:'cg*max(pulse(t*k,0.08),0.6*pulse(t*k-0.2,0.08))',c:'cb*max(pulse(t*k,0.08),0.6*pulse(t*k-0.2,0.08))'},
  sunrise:{space:'hsv',a:'mix(0,45,smooth(t*k/60))',b:'mix(1,0.6,smooth(t*k/60))',c:'smooth(t*k/60)'},
  'color-breathe':{space:'hsv',a:'ch',b:'cs',c:'cv*(0.1+0.9*wave(t*k*0.25))'},
};
function setEditor(s){
  $('sName').value=s.name||'';$('sSpace').value=s.space||'hsv';$('sA').value=s.a||'';$('sB').value=s.b||'';$('sC').value=s.c||'';
  labels();compilePreview();
}
function editorScript(){return{name:$('sName').value.trim(),space:$('sSpace').value,a:$('sA').value.trim(),b:$('sB').value.trim(),c:$('sC').value.trim()}}
function labels(){const h=$('sSpace').value==='hsv';$('aL').textContent=h?'Hue (degrees)':'Red (0-1)';$('bL').textContent=h?'Saturation (0-1)':'Green (0-1)';$('cL').textContent=h?'Value (0-1)':'Blue (0-1)'}
$('sSpace').onchange=()=>{labels();compilePreview()};

function loadScripts(select){
  return api('/api/scripts').then(list=>{
    scriptList=list;
    $('sPick').innerHTML='<option value="">— saved scripts ('+list.length+') —</option>'+list.map(s=>'<option>'+esc(s.name)+'</option>').join('');
    if(select)$('sPick').value=select;
  });
}
$('sPick').onchange=()=>{const s=scriptList.find(x=>x.name===$('sPick').value);if(s)setEditor(s)};
$('sNew').onclick=()=>{$('sPick').value='';setEditor({space:'hsv',a:'t*k*30',b:'1',c:'1'});$('sName').focus()};
for(const [n,s] of Object.entries(EXAMPLES)){const b=document.createElement('button');b.textContent=n;b.onclick=()=>setEditor(Object.assign({name:n},s));$('examples').appendChild(b)}

// Browser preview: TinyExpr syntax matches JavaScript for these characters once ^ becomes **.
const fr=x=>x-Math.floor(x),sm=x=>{x=Math.min(1,Math.max(0,x));return x*x*(3-2*x)};
const hash1=i=>{let h=Math.imul(i|0,0x9E3779B1);h^=h>>>15;h=Math.imul(h,0x85EBCA77);h^=h>>>13;return(h&0xFFFF)/65535};
const FN={frac:fr,tri:x=>{const f=fr(x);return f<.5?f*2:2-f*2},sq:x=>fr(x)<.5?1:0,pulse:(x,d)=>fr(x)<d?1:0,wave:x=>.5+.5*Math.sin(x*2*Math.PI),
  clamp:(x,a,b)=>Math.min(b,Math.max(a,x)),mix:(a,b,f)=>a+(b-a)*f,smooth:sm,noise:x=>{const i=Math.floor(x);return FN.mix(hash1(i),hash1(i+1),sm(x-i))},
  rand:()=>Math.random(),sel:(c,a,b)=>c>0?a:b,min:Math.min,max:Math.max,abs:Math.abs,floor:Math.floor,ceil:Math.ceil,sin:Math.sin,cos:Math.cos,tan:Math.tan,
  sqrt:Math.sqrt,pow:Math.pow,exp:Math.exp,ln:Math.log,log:Math.log10,log10:Math.log10,atan:Math.atan,atan2:Math.atan2,asin:Math.asin,acos:Math.acos,pi:Math.PI,e:Math.E};
let pvFns=null;
function compilePreview(){
  try{
    pvFns=['sA','sB','sC'].map(id=>{
      const src=$(id).value.trim();
      if(!src)throw new Error('all three formulas are required');
      // only formula characters, so a stored script can never run other code in the browser
      if(!/^[\w\s.+\-*\/%^<>=!&|(),]*$/.test(src))throw new Error('unsupported character');
      return new Function('t','speed','k','cr','cg','cb','ch','cs','cv','F','with(F){return +('+src.replace(/\^/g,'**')+')}');
    });
    $('pvMsg').className='';$('pvMsg').textContent='Live preview in the browser (last 5 seconds, newest on the right).';
  }catch(e){pvFns=null;$('pvMsg').className='err';$('pvMsg').textContent='Preview: '+e.message}
}
for(const id of['sA','sB','sC'])$(id).oninput=compilePreview;
function hsv2rgb(h,s,v){h=((h%360)+360)%360;s=Math.min(1,Math.max(0,s));v=Math.min(1,Math.max(0,v));const c=v*s,x=c*(1-Math.abs(h/60%2-1)),m=v-c;
  const [r,g,b]=h<60?[c,x,0]:h<120?[x,c,0]:h<180?[0,c,x]:h<240?[0,x,c]:h<300?[x,0,c]:[c,0,x];return[r+m,g+m,b+m]}
function pvColor(t){
  const c=st.color||{r:255,g:255,b:255},cr=c.r/255,cg=c.g/255,cb=c.b/255,mx=Math.max(cr,cg,cb),mn=Math.min(cr,cg,cb),d=mx-mn;
  const ch=d===0?0:mx===cr?60*(((cg-cb)/d+6)%6):mx===cg?60*((cb-cr)/d+2):60*((cr-cg)/d+4),sp=st.speed||128;
  const v=pvFns.map(f=>{const r=f(t,sp,sp/128,cr,cg,cb,ch,mx?d/mx:0,mx,FN);return isNaN(r)?0:r});
  const rgb=$('sSpace').value==='hsv'?hsv2rgb(v[0],v[1],v[2]):v.map(x=>Math.min(1,Math.max(0,x)));
  return 'rgb('+rgb.map(x=>Math.round(x*255)).join(',')+')';
}
const pvStart=performance.now(),ctx=$('pv').getContext('2d');
function drawPreview(){
  requestAnimationFrame(drawPreview);
  if(!pvFns||$('app').hidden)return;
  const now=(performance.now()-pvStart)/1000,W=$('pv').width,H=$('pv').height;
  try{
    for(let x=0;x<W;x+=4){ctx.fillStyle=pvColor(Math.max(0,now-5+5*x/W));ctx.fillRect(x,0,4,H)}
    $('pvSwatch').style.background=pvColor(now);
  }catch(e){pvFns=null;$('pvMsg').className='err';$('pvMsg').textContent='Preview: '+e.message}
}
requestAnimationFrame(drawPreview);

$('sTry').onclick=()=>{const s=editorScript();$('sMsg').className='dim';$('sMsg').textContent='Sending…';
  api('/api/scripts/preview','POST',{space:s.space,a:s.a,b:s.b,c:s.c,duration:15000})
    .then(r=>{st=r;render();$('sMsg').textContent='Running on the LEDs for 15 s.'})
    .catch(e=>{$('sMsg').className='err';$('sMsg').textContent=e.message})};
$('sSave').onclick=()=>{const s=editorScript();$('sMsg').className='dim';$('sMsg').textContent='Saving…';
  api('/api/scripts','POST',s).then(()=>Promise.all([loadScripts(s.name),loadModes()]))
    .then(()=>$('sMsg').textContent='Saved. "'+s.name+'" is now in the effect list and in Home Assistant.')
    .catch(e=>{$('sMsg').className='err';$('sMsg').textContent=e.message})};
$('sDel').onclick=()=>{const n=$('sName').value.trim();if(!n||!confirm('Delete script "'+n+'"?'))return;
  api('/api/scripts?name='+encodeURIComponent(n),'DELETE').then(()=>Promise.all([loadScripts(),loadModes()]))
    .then(()=>{$('sMsg').className='dim';$('sMsg').textContent='Deleted.';setEditor({space:'hsv'})})
    .catch(e=>{$('sMsg').className='err';$('sMsg').textContent=e.message})};

// ---------- device, Wi-Fi, MQTT ----------
function loadInfo(){
  return api('/api/info').then(i=>{
    $('wifiInfo').textContent=i.ap_mode?'Setup mode: pick your network below.':'Connected to '+i.ssid+' ('+i.rssi+' dBm), IP '+i.ip;
    if(i.ap_mode)$('wifiBox').open=true;
    $('pwWarn').hidden=!i.default_password;
    if(!$('host').value)$('host').value=i.hostname;
    const up=Math.floor(i.uptime/1000);
    $('dev').innerHTML='Firmware '+esc(i.version)+'<br>'+esc(i.hostname)+'.local &middot; '+esc(i.mac)+'<br>Uptime '+Math.floor(up/3600)+'h '+Math.floor(up%3600/60)+'m &middot; free heap '+i.free_heap+' B';
  });
}
function scan(){
  $('nets').innerHTML='<span class="dim">Scanning…</span>';
  api('/api/wifi/scan').then(r=>{
    if(r.scanning){setTimeout(scan,1500);return}
    $('nets').innerHTML='';
    r.networks.forEach(n=>{const b=document.createElement('button');b.innerHTML='<span>'+esc(n.ssid)+'</span><span class="dim">'+n.rssi+' dBm'+(n.secure?' 🔒':'')+'</span>';b.onclick=()=>{$('ssid').value=n.ssid;$('pass').focus()};$('nets').appendChild(b)});
    if(!r.networks.length)$('nets').innerHTML='<span class="dim">No networks found</span>';
  }).catch(e=>$('nets').textContent=e.message);
}
$('pwWarn').onclick=()=>{
  $('pwBox').open=true;
  $('pwBox').scrollIntoView({behavior:'smooth',block:'center'});
  setTimeout(()=>$('pwCur').focus(),400);
};
$('scan').onclick=scan;
$('save').onclick=()=>{
  if(!$('ssid').value){$('msg').textContent='Enter a network name.';return}
  $('msg').textContent='Saving…';
  api('/api/wifi','POST',{ssid:$('ssid').value,password:$('pass').value,hostname:$('host').value})
    .then(r=>$('msg').textContent='Saved. Rebooting and joining '+$('ssid').value+'. Then open http://'+r.hostname+'.local')
    .catch(e=>$('msg').textContent='Error: '+e.message);
};
function loadMqtt(){
  return api('/api/mqtt').then(m=>{
    $('mqttInfo').textContent=!m.enabled?'Disabled':(m.connected?'Connected to '+m.host+' · topic '+m.base_topic:'Not connected to '+m.host+':'+m.port);
    if(!$('mqttBox').open){
      $('mHost').value=m.host;$('mPort').value=m.port;$('mUser').value=m.user;$('mEn').checked=m.enabled;
      $('mPass').placeholder=m.password_set?'Password (leave empty to keep)':'Password (optional)';
    }
  });
}
$('mSave').onclick=()=>{
  const b={enabled:$('mEn').checked,host:$('mHost').value.trim(),port:+($('mPort').value||1883),user:$('mUser').value};
  if($('mPass').value)b.password=$('mPass').value;
  $('mMsg').textContent='Saving…';
  api('/api/mqtt','POST',b).then(()=>{$('mMsg').textContent='Saved.';$('mPass').value='';setTimeout(loadMqtt,3000)}).catch(e=>$('mMsg').textContent='Error: '+e.message);
};

// ---------- security ----------
$('pwSave').onclick=()=>{
  const cur=$('pwCur').value,nw=$('pwNew').value;
  if(nw.length<8||nw.length>63){$('pwMsg').textContent='New password must be 8-63 characters.';return}
  if(nw!==$('pwNew2').value){$('pwMsg').textContent='New passwords do not match.';return}
  $('pwMsg').textContent='Saving…';
  api('/api/login').then(c=>api('/api/password','POST',{nonce:c.nonce,proof:md5(md5(c.user+':'+c.realm+':'+cur)+':'+c.nonce),
      ha1:md5(c.user+':'+c.realm+':'+nw),ota:md5(nw)}))
    .then(()=>{$('pwMsg').textContent='Changed. The device reboots; log in again with the new password.';setTimeout(()=>location.reload(),6000)})
    .catch(e=>$('pwMsg').textContent='Error: '+e.message);
};
function loadTokens(){
  return api('/api/tokens').then(ts=>{
    $('tokList').innerHTML = ts.length ? '' : 'No tokens yet.';
    ts.forEach(t=>{
      const row=document.createElement('div');row.className='btns';row.style.margin='6px 0';
      row.innerHTML='<span style="flex:1">'+esc(t.name)+' <span class="dim">('+esc(t.scope)+')</span></span>';
      const b=document.createElement('button');b.className='danger';b.textContent='Revoke';
      b.onclick=()=>{if(confirm('Revoke "'+t.name+'"?'))api('/api/tokens?id='+encodeURIComponent(t.id),'DELETE').then(loadTokens)};
      row.appendChild(b);$('tokList').appendChild(row);
    });
  });
}
$('tokBox').addEventListener('toggle',()=>{if($('tokBox').open)loadTokens()});
$('tokAdd').onclick=()=>{
  $('tokNew').className='dim';$('tokNew').textContent='Creating…';
  api('/api/tokens','POST',{name:$('tokName').value.trim(),scope:$('tokScope').value})
    .then(r=>{$('tokNew').innerHTML='<b>'+esc(r.token)+'</b><br>Copy it now — it is not shown again.';$('tokName').value='';loadTokens()})
    .catch(e=>{$('tokNew').className='err';$('tokNew').textContent=e.message});
};
$('apSave').onclick=()=>{
  $('apMsg').textContent='Saving…';
  api('/api/ap-password','POST',{password:$('apNew').value})
    .then(()=>{$('apMsg').textContent='Changed.';$('apNew').value=''}).catch(e=>$('apMsg').textContent='Error: '+e.message);
};

// ---------- startup ----------
function start(){
  loadInfo().then(()=>{
    $('app').hidden=false;
    if(started)return;
    started=true;
    loadModes();loadState();loadMqtt();loadScripts();
    setEditor(Object.assign({name:'fire'},EXAMPLES.fire));
    setInterval(()=>{if(!$('app').hidden){loadInfo();loadMqtt();if(st.temporary)loadState()}},15000);
  }).catch(()=>{});
}
start();
</script></body></html>)rawliteral";

const char UPDATE_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DeskLED update</title>
<style>body{margin:0;background:#111316;color:#e8eaed;font:15px/1.4 system-ui,sans-serif;padding:16px}
main{max-width:460px;margin:0 auto}section{background:#1b1e23;border:1px solid #2a2e35;border-radius:12px;padding:14px}
button{font:inherit;background:#ff9a3c;border:0;border-radius:8px;padding:9px 12px;font-weight:600;color:#111;margin-top:10px}
input{max-width:100%}.dim{color:#8a919c}a{color:#ff9a3c}</style></head>
<body><main><h1>Firmware update</h1><section>
<p class="dim">Upload <code>firmware.signed.bin</code>. Unsigned or foreign images are rejected. Log in on the <a href="/">main page</a> first.</p>
<input type="file" id="f" accept=".bin"><br><button id="go">Upload</button>
<p id="msg" class="dim"></p><p><a href="/">Back</a></p></section></main>
<script>
document.getElementById('go').onclick=()=>{
  const file=document.getElementById('f').files[0],msg=document.getElementById('msg');
  if(!file){msg.textContent='Choose a file first.';return}
  const fd=new FormData();fd.append('firmware',file);
  const x=new XMLHttpRequest();x.open('POST','/update');x.setRequestHeader('X-DeskLED','1');
  x.upload.onprogress=e=>msg.textContent='Uploading '+Math.round(e.loaded*100/e.total)+'%';
  x.onload=()=>{let r={};try{r=JSON.parse(x.responseText)}catch(e){}
    msg.textContent=x.status==200?'Done. Rebooting…':x.status==401?'Not logged in. Log in on the main page first.':'Failed: '+(r.error||x.status);
    if(x.status==200)setTimeout(()=>location.href='/',15000)};
  x.onerror=()=>msg.textContent='Upload failed (connection error).';
  x.send(fd);
};
</script></body></html>)rawliteral";
