#pragma once
#include "esphome/core/progmem.h"

namespace esphome::web_server {

static const char LOGS_INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Device Logs</title>
<style>
*{box-sizing:border-box}
body{font-family:system-ui,-apple-system,sans-serif;background:#0b1120;color:#e2e8f0;margin:0;padding:16px;font-size:13px;line-height:1.5}
.card{background:#1e293b;border:1px solid #334155;border-radius:12px;padding:16px;max-width:920px;margin:0 auto 16px;box-shadow:0 4px 6px -1px rgba(0,0,0,0.3)}
.hdr{display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px}
.title{font-size:18px;font-weight:700;color:#f8fafc;margin:0;display:flex;align-items:center;gap:8px}
.sub{color:#94a3b8;font-size:11px;margin-top:4px;display:flex;flex-wrap:wrap;gap:12px}
.btns{display:flex;gap:6px;flex-wrap:wrap}
.btn{background:#334155;color:#e2e8f0;border:1px solid #475569;padding:6px 12px;border-radius:6px;cursor:pointer;font-size:12px;text-decoration:none;display:inline-flex;align-items:center;gap:4px;transition:all .2s}
.btn:hover{background:#475569}.btn.act{background:#059669;border-color:#10b981;color:#fff}
table{width:100%;border-collapse:collapse;margin-top:10px;font-size:12px}
th,td{padding:8px 10px;text-align:left;border-bottom:1px solid #334155}
th{color:#94a3b8;font-size:10px;text-transform:uppercase;letter-spacing:.05em}
tr:hover td{background:rgba(255,255,255,.02)}
.b{padding:2px 8px;border-radius:10px;font-size:11px;font-weight:600;display:inline-block}
.b-on{background:#065f46;color:#6ee7b7}.b-off{background:#334155;color:#94a3b8}
.b-mqtt{background:#075985;color:#7dd3fc}.b-local{background:#065f46;color:#6ee7b7}.b-web{background:#581c87;color:#d8b4fe}.b-ha{background:#0369a1;color:#bae6fd}.b-oth{background:#334155;color:#cbd5e1}
.b-pwr{background:#1e3a8a;color:#93c5fd}.b-sw{background:#065f46;color:#6ee7b7}.b-wdt{background:#881337;color:#fecdd3}.b-rst{background:#475569;color:#cbd5e1}
.tab{background:none;border:1px solid #334155;color:#94a3b8;padding:4px 8px;border-radius:4px;cursor:pointer;font-size:11px;margin-right:4px;transition:all .2s}
.tab:hover{color:#fff;border-color:#64748b}.tab.act{background:#2563eb;color:#fff;border-color:#2563eb}
.pulse{display:inline-block;width:8px;height:8px;border-radius:50%;background:#10b981;box-shadow:0 0 0 rgba(16,185,129,.7);animation:pulse 2s infinite}
@keyframes pulse{0%{box-shadow:0 0 0 0 rgba(16,185,129,.7)}70%{box-shadow:0 0 0 6px rgba(16,185,129,0)}100%{box-shadow:0 0 0 0 rgba(16,185,129,0)}}
</style>
</head>
<body>
<div class="card">
  <div class="hdr">
    <div>
      <h1 class="title"><span class="pulse"></span> <span id="dn">Device Logs</span> <span class="b" style="background:#0284c7;color:#fff;font-size:10px">Live</span></h1>
      <div class="sub" id="ds">Connecting...</div>
    </div>
    <div class="btns">
      <button class="btn" onclick="ld()">&#8635; Refresh</button>
      <button id="ab" class="btn" onclick="tA()">&#9889; Auto 3s</button>
      <a class="btn" href="/device_info">Info</a>
      <a class="btn" href="/">Home</a>
    </div>
  </div>
</div>

<div class="card">
  <div class="hdr"><b style="color:#94a3b8;font-size:11px;text-transform:uppercase">&#128260; Last 3 Restarts</b><span id="bc" style="color:#64748b;font-size:11px"></span></div>
  <div style="overflow-x:auto"><table><thead><tr><th>#</th><th>Timestamp</th><th>Restart Reason</th><th>Boot #</th></tr></thead><tbody id="rb"><tr><td colspan="4" style="color:#64748b">Loading...</td></tr></tbody></table></div>
</div>

<div class="card">
  <div class="hdr">
    <b style="color:#94a3b8;font-size:11px;text-transform:uppercase">Switch State History (Last 10)</b>
    <div id="tabs"><button class="tab act" onclick="flt('all')">All</button></div>
  </div>
  <div style="overflow-x:auto"><table><thead><tr><th>#</th><th>Timestamp</th><th>Switch</th><th>State</th><th>Cause</th><th>Uptime</th></tr></thead><tbody id="sb"><tr><td colspan="6" style="color:#64748b">Loading...</td></tr></tbody></table></div>
  <div style="text-align:center;margin-top:12px;font-size:11px"><a href="/logs?format=json" style="color:#38bdf8;text-decoration:none">JSON API Endpoint</a></div>
</div>

<script>
let tm=0,curF='all',rawD=null;
function tA(){let b=document.getElementById('ab');if(tm){clearInterval(tm);tm=0;b.classList.remove('act');b.innerHTML='&#9889; Auto 3s';}else{tm=setInterval(ld,3000);b.classList.add('act');b.innerHTML='&#9889; Running';}}
function flt(n){curF=n;document.querySelectorAll('#tabs .tab').forEach(t=>t.classList.toggle('act',t.textContent===n||(n==='all'&&t.textContent==='All')));rSw();}
function ld(){fetch('/logs?format=json').then(r=>r.json()).then(d=>{rawD=d;rUI();}).catch(e=>{document.getElementById('ds').innerText='Error loading logs';});}
function rUI(){
  if(!rawD)return;
  document.title=(rawD.device||'Device')+' - Logs';
  document.getElementById('dn').innerText=(rawD.device||'Device');
  document.getElementById('ds').innerHTML='<span>IP: '+(rawD.ip||'')+'</span><span>MAC: '+(rawD.mac||'')+'</span>';
  document.getElementById('bc').innerText='Total Boots: '+(rawD.total_boots||1);
  let rBody=document.getElementById('rb');
  if(!rawD.restarts||rawD.restarts.length===0){rBody.innerHTML='<tr><td colspan="4" style="color:#64748b">No restarts recorded</td></tr>';}
  else{rBody.innerHTML=rawD.restarts.map((r,i)=>{
    let bc='b-rst',s=r.reason||'';if(s.includes('Power'))bc='b-pwr';else if(s.includes('Software')||s.includes('System'))bc='b-sw';else if(s.includes('Watchdog')||s.includes('Exception'))bc='b-wdt';
    return '<tr><td><b>'+(i+1)+'</b>'+(i===0?' <span style="font-size:9px;color:#38bdf8">(Current)</span>':'')+'</td><td>'+r.timestamp+'</td><td><span class="b '+bc+'">'+s+'</span></td><td style="color:#64748b;font-family:monospace">#'+r.boot_number+'</td></tr>';
  }).join('');}
  let sws=[...new Set((rawD.logs||[]).map(l=>l.switch))];
  let th='<button class="tab '+(curF==='all'?'act':'')+'" onclick="flt(\'all\')">All</button>';
  sws.forEach(s=>{th+='<button class="tab '+(curF===s?'act':'')+'" onclick="flt(\''+s+'\')">'+s+'</button>';});
  document.getElementById('tabs').innerHTML=th;
  rSw();
}
function rSw(){
  if(!rawD)return;
  let sBody=document.getElementById('sb');
  let fl=(rawD.logs||[]).filter(l=>curF==='all'||l.switch===curF);
  if(fl.length===0){sBody.innerHTML='<tr><td colspan="6" style="color:#64748b;text-align:center;padding:16px">No switch changes</td></tr>';return;}
  sBody.innerHTML=fl.map((l,i)=>{
    let cc='b-oth',c=l.cause||'';if(c==='mqtt')cc='b-mqtt';else if(c==='local switch')cc='b-local';else if(c==='web server')cc='b-web';else if(c==='home assistant')cc='b-ha';
    let is_on=(l.state===true||l.state==='ON'||l.state_str==='ON');
    let sb=is_on?'<span class="b b-on">ON</span>':'<span class="b b-off">OFF</span>';
    return '<tr><td><b>'+(i+1)+'</b></td><td>'+l.timestamp+'</td><td><code>'+l.switch+'</code></td><td>'+sb+'</td><td><span class="b '+cc+'">'+c+'</span></td><td style="color:#64748b;font-family:monospace">'+l.uptime_ms+' ms</td></tr>';
  }).join('');
}
ld();
</script>
</body>
</html>
)rawliteral";

}  // namespace esphome::web_server
