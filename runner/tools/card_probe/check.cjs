/* End-to-end check in an isolated Chrome profile and emulator save namespace.
 * Build this folder first. Cached 4.2.3 assets in /tmp are optional; the CDN
 * supplies them otherwise. No production browser profile or card is opened. */
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const {spawn} = require('node:child_process');
const assert = require('node:assert/strict');
const out = path.resolve(__dirname, '../../build/card-probe');
const repo = path.resolve(__dirname, '../../..');
const playerMode = process.argv.includes('--player');
const logs = [];
const pageSource=fs.readFileSync(path.resolve(__dirname,'../../../web/index.html'),'utf8');
const frameLock=pageSource.slice(pageSource.indexOf('    function lockFrameSize('),pageSource.indexOf('    function boot('));
const cardBridge=pageSource.slice(pageSource.indexOf('    let cardGen ='),pageSource.indexOf('    function unlockAudio('));
const cachedPaths=Object.fromEntries([
 ['emulator.min.js','/private/tmp/ejs.min.js','/emulator.min.js'],
 ['pcsx_rearmed-thread-wasm.data','/private/tmp/pcsx.data','/pcsx.data']
].filter(([,file])=>fs.existsSync(file)).map(([name,,url])=>[name,url]));
const html = `<!doctype html><meta charset="utf-8"><div id="game" style="width:640px;height:480px"></div>
<script>
window.probeLogs=[];
const log=console.log;
console.log=(...args)=>{probeLogs.push(args.map(String).join(' '));log(...args)};
window.EJS_player='#game'; window.EJS_core='psx';window.EJS_gameName='runner-card-probe';
window.EJS_gameUrl='/card-probe.bin';window.EJS_pathtodata='https://cdn.emulatorjs.org/4.2.3/data/';
window.EJS_startOnLoaded=true;window.EJS_disableAutoLang=false;window.EJS_threads=true;window.EJS_volume=0;
window.EJS_onGameStart=()=>{EJS_emulator.debug=true;console.log('WEB_CORE_START')};
Object.defineProperty(window,'EJS_GameManager',{
 configurable:true,get:()=>window.probeManager,
 set(Manager){
  window.probeManager=Manager;
  const mount=mountCardFileSystem;
  Manager.prototype.mountFileSystems=async function(){
   await mount.call(this);
   const file='/data/saves/PCSX-ReARMed/card-probe.srm';
   const card=this.FS.analyzePath(file).exists?this.FS.readFile(file):null;
   window.restoredCard=card?{bytes:card.length,header:String.fromCharCode(card[0],card[1]),reach:card[8192+0x208]}:null;
  };
  const cfg=Manager.prototype.getRetroArchCfg;
  Manager.prototype.getRetroArchCfg=function(){return cfg.call(this)+'audio_sync = false\\nblock_sram_overwrite = true\\n'};
 }
});
${cardBridge}
${frameLock}
lockFrameSize({width:320,height:240});
window.readProbe=async()=>{
 const png=await EJS_emulator.gameManager.screenshot();
 const bmp=await createImageBitmap(new Blob([png],{type:'image/png'}));
 const c=document.createElement('canvas');c.width=320;c.height=240;
 const g=c.getContext('2d',{willReadFrequently:true});g.imageSmoothingEnabled=false;
 g.drawImage(bmp,0,0,320,240);bmp.close();
 const p=g.getImageData(0,239,320,1).data,b=new Uint8Array(40);
 for(let i=0;i<320;i++)if(p[i*4]>=128&&p[i*4+1]>=128&&p[i*4+2]>=128)b[i>>3]|=128>>(i&7);
 return {magic:new TextDecoder().decode(b.slice(0,4)),ready:b[4],reach:b[5],generation:b[6],frame:b[8]+b[9]*256,pad:{status:b[10],type:b[11],buttons:b[12]+b[13]*256}};
};
window.EJS_paths=${JSON.stringify(cachedPaths)};
</script><script src="https://cdn.emulatorjs.org/4.2.3/data/loader.js"></script>`;
const server = http.createServer((req,res)=>{
  res.setHeader('Cross-Origin-Opener-Policy','same-origin');
  res.setHeader('Cross-Origin-Embedder-Policy','require-corp');
  res.setHeader('Cache-Control','no-cache');
  const route=new URL(req.url,'http://localhost').pathname;
  if(route==='/') {res.setHeader('Content-Type','text/html');res.end(playerMode?pageSource:html);return;}
  const files={
    '/card-probe.bin':path.join(out,'card-probe.bin'),
    '/emulator.min.js':'/private/tmp/ejs.min.js',
    '/pcsx.data':'/private/tmp/pcsx.data',
    '/manifest.json':path.join(repo,'web/manifest.json'),
    '/build/runner-web.bin':path.join(repo,'build/runner-web.bin')
  };
  const asset=route.startsWith('/brand/')?path.join(repo,'web',decodeURIComponent(route)):route.startsWith('/runner/audio/')?path.join(repo,decodeURIComponent(route)):null;
  const file=files[route]||asset;
  if(file && fs.existsSync(file)) {
    res.setHeader('Content-Type',({'.js':'text/javascript','.json':'application/json','.mp3':'audio/mpeg','.ttf':'font/ttf','.png':'image/png'})[path.extname(file)]||'application/octet-stream');
    res.setHeader('Content-Length',fs.statSync(file).size);
    if(req.method==='HEAD'){res.end();return;}
    fs.createReadStream(file).pipe(res);
  } else {res.writeHead(404);res.end();}
});

async function run() {
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const url=`http://127.0.0.1:${server.address().port}/`;
  const chrome=spawn('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',[
    '--headless=new','--no-first-run','--no-default-browser-check','--disable-background-networking',
    '--autoplay-policy=no-user-gesture-required',`--user-data-dir=${path.join(out,'browser-profile')}`,
    '--remote-debugging-pipe','about:blank'
  ],{stdio:['ignore','ignore','pipe','pipe','pipe']});
  const pending=new Map();let next=0,buffer='';
  chrome.on('exit',()=>{
    for(const {reject} of pending.values())reject(Error('Headless Chrome exited'));
    pending.clear();
  });
  chrome.stderr.on('data',data=>logs.push(String(data)));
  chrome.stdio[4].on('data',data=>{
    buffer+=data;
    let at;
    while((at=buffer.indexOf('\0'))>=0) {
      const raw=buffer.slice(0,at);buffer=buffer.slice(at+1);
      if(!raw)continue;
      const msg=JSON.parse(raw);
      if(msg.id && pending.has(msg.id)) {
        const {resolve,reject}=pending.get(msg.id);pending.delete(msg.id);
        msg.error?reject(Error(JSON.stringify(msg.error))):resolve(msg.result);
      }
      if(msg.method==='Runtime.consoleAPICalled')
        logs.push(msg.params.args.map(a=>a.value??a.description).join(' '));
      if(msg.method==='Runtime.exceptionThrown') logs.push(JSON.stringify(msg.params));
      if(msg.method==='Network.loadingFailed') logs.push(JSON.stringify(msg.params));
    }
  });
  const send=(method,params={},sessionId)=>new Promise((resolve,reject)=>{
    const id=++next;pending.set(id,{resolve,reject});
    chrome.stdio[3].write(JSON.stringify({id,method,params,sessionId})+'\0');
  });
  const watchdog=setTimeout(()=>chrome.kill(),120000);
  try {
    const {targetId}=await send('Target.createTarget',{url:'about:blank'});
    const {sessionId}=await send('Target.attachToTarget',{targetId,flatten:true});
    for(const method of ['Runtime.enable','Page.enable','Network.enable']) await send(method,{},sessionId);
    await send('Page.bringToFront',{},sessionId);
    const evaluate=async expression=>{
      const result=await send('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true},sessionId);
      if(result.exceptionDetails)throw Error(JSON.stringify(result.exceptionDetails));
      return result.result.value;
    };
    const wait=async(test,label)=>{
      const start=Date.now();
      while(Date.now()-start<45000) {
        if(await evaluate(test))return;
        await new Promise(resolve=>setTimeout(resolve,300));
      }
      const diagnostic=await evaluate(`(()=>{const e=window.EJS_emulator,g=e?.gameManager;return {started:e?.started,paused:e?.paused,frame:g?.getFrameNum(),path:g?.getSaveFilePath(),logs:window.probeLogs}})()`);
      const shot=await send('Page.captureScreenshot',{format:'png'},sessionId);
      fs.writeFileSync(path.join(out,'failure.png'),Buffer.from(shot.data,'base64'));
      throw Error('Timed out: '+label+'\n'+JSON.stringify(diagnostic));
    };
    if(playerMode) {
      await send('Page.addScriptToEvaluateOnNewDocument',{source:`window.EJS_paths=${JSON.stringify(cachedPaths)};`},sessionId);
      await send('Page.navigate',{url:url+'?game=runner'},sessionId);
      await wait('!!document.querySelector("#play:not(:disabled)")','Play button');
      assert.equal(await evaluate('document.getElementById("card-status").hidden'),true,'Card status shown before Play');
      // A stale ROM with the same HTTP length must not replace this build.
      await evaluate(`(async()=>{
        const size=Number((await fetch('/build/runner-web.bin',{method:'HEAD'})).headers.get('content-length'));
        const data=new Uint8Array(size),probe=new Uint8Array(await (await fetch('/card-probe.bin')).arrayBuffer());data.set(probe);
        await new EJS_STORAGE('EmulatorJS-roms','rom').put('runner-web.bin',{'content-length':String(size),data});
      })()`);
      assert.equal(await evaluate('EJS_emulator.storage.rom instanceof EJS_DUMMYSTORAGE'),true,'Old ROM cache can still override the current disc');
      await evaluate('document.getElementById("play").click()');
      await wait('!!window.EJS_emulator?.started','player emulator start');
      await evaluate(`window.inputLog=[];const gm=EJS_emulator.gameManager,send=gm.simulateInput;gm.simulateInput=function(...args){inputLog.push(args);return send.apply(this,args)}`);
      const key=async(code)=>{
        await send('Input.dispatchKeyEvent',{type:'keyDown',code,key:code==='Enter'?'Enter':code.slice(-1).toLowerCase(),windowsVirtualKeyCode:code==='Enter'?13:code.charCodeAt(code.length-1)},sessionId);
        await new Promise(r=>setTimeout(r,150));
        await send('Input.dispatchKeyEvent',{type:'keyUp',code,key:code==='Enter'?'Enter':code.slice(-1).toLowerCase(),windowsVirtualKeyCode:code==='Enter'?13:code.charCodeAt(code.length-1)},sessionId);
        await new Promise(r=>setTimeout(r,1000));
      };
      await wait('!!window.__music?.el.src','menu music');
      assert.equal(await evaluate('document.getElementById("song-credit").hidden'),true,'Menu music showed a credit popover');
      console.log('Menu audio:',await evaluate('window.__music.el.src'));
      for(let i=0;i<14 && await evaluate('__music.el.src.includes("Digital%20Atmosphere")');i++)await key('Enter');
      console.log('Input trace:',await evaluate('JSON.stringify(inputLog)'));
      const inputShot=await send('Page.captureScreenshot',{format:'png'},sessionId);
      fs.writeFileSync(path.join(out,'player-input.png'),Buffer.from(inputShot.data,'base64'));
      await wait('!!window.__music.el.src && !window.__music.el.src.includes("Digital%20Atmosphere")','gameplay music');
      await new Promise(r=>setTimeout(r,3500));
      await key('Enter'); // pause before a block can end the run
      const sample=`(async()=>({src:__music.el.src,canvas:parseRow(readCanvasRow({width:320,height:240})),shot:parseRow(await readShotRow({width:320,height:240}))}))()`;
      const before=await evaluate(sample);console.log('Before shoulder:',JSON.stringify(before));
      await key('KeyE');
      const next=await evaluate(sample);console.log('After E:',JSON.stringify(next));
      await key('KeyQ');
      const previous=await evaluate(sample);console.log('After Q:',JSON.stringify(previous));
      assert.notEqual(next.src,before.src,'E did not change web audio');
      assert.equal(previous.src,before.src,'Q did not return to previous song');
      assert.notDeepEqual(next.shot,before.shot,'Game pixel row did not change');
      await evaluate(`window.testPad={id:'DualSense test pad',index:0,connected:true,mapping:'standard',axes:[0,0,0,0],buttons:Array.from({length:18},()=>({pressed:false,value:0}))};Object.defineProperty(navigator,'getGamepads',{configurable:true,value:()=>[testPad]})`);
      const shoulder=async(button)=>{
        await evaluate(`testPad.buttons[${button}]={pressed:true,value:1}`);
        await new Promise(r=>setTimeout(r,200));
        await evaluate(`testPad.buttons[${button}]={pressed:false,value:0}`);
        await new Promise(r=>setTimeout(r,1000));
        return evaluate(sample);
      };
      const padNext=await shoulder(5),padPrevious=await shoulder(4);
      console.log('Controller R1:',JSON.stringify(padNext));
      console.log('Controller L1:',JSON.stringify(padPrevious));
      assert.equal(padNext.src,next.src,'Controller R1 did not advance music');
      assert.equal(padPrevious.src,before.src,'Controller L1 did not go back');
      assert.equal(await evaluate('!__music.el.paused && __music.el.currentTime>0'),true,'Selected audio is not playing');
      fs.writeFileSync(path.join(out,'player-result.json'),JSON.stringify({before,next,previous,padNext,padPrevious},null,2));
      console.log('PASS: real web page, ROM, keyboard and controller shoulders, pixel commands and audio changes.');
      return;
    }
    await send('Page.navigate',{url},sessionId);
    await wait('!!window.EJS_emulator?.started','emulator start');
    console.log('Emulator running; waiting for the production save code to write.');
    await wait('readProbe().then(p=>p.magic==="CARD"&&p.ready&&(p.generation||p.reach===7))','card write');
    await evaluate('EJS_emulator.gameManager.simulateInput(0,11,1)');
    await new Promise(r=>setTimeout(r,300));
    const input=await evaluate('readProbe()');
    assert.equal(input.pad.status,0);assert.equal(input.pad.buttons & (1<<11),0,'R1 did not reach the BIOS pad report');
    await evaluate('EJS_emulator.gameManager.simulateInput(0,11,0)');
    const inspect=`(async()=>{
      const gm=EJS_emulator.gameManager;
      const probe=await readProbe();
      if(probe.generation)noteCard([{id:3,args:[probe.generation]}]);
      const committed=await flushCard();
      const card=gm.getSaveFile(false);let block=0;
      if(card)for(let i=1;i<=15;i++)if(new TextDecoder().decode(card.slice(i*128+10,i*128+30)).startsWith('UUUVVV-RUNNER'))block=i;
      return {path:gm.getSaveFilePath(),bytes:card?.length,header:card?String.fromCharCode(card[0],card[1]):'',
              block,reach:block?card[block*8192+0x208]:0,probe,committed,bridge:window.__card,restored:window.restoredCard};
    })()`;
    const first=await evaluate(inspect);
    console.log('First boot:',JSON.stringify(first));
    assert.equal(first.bytes,131072);assert.equal(first.header,'MC');assert.equal(first.reach,7);
    assert.equal(first.probe.generation,1,'First boot must create a new save');
    assert.equal(first.committed,true);assert.equal(first.bridge.persisted,true);assert.equal(first.bridge.generation,1);
    const previousOrigin=await evaluate('performance.timeOrigin');
    await send('Page.reload',{},sessionId);
    await wait(`performance.timeOrigin!==${previousOrigin} && !!window.EJS_emulator?.started`,'second emulator start');
    await wait('readProbe().then(p=>p.magic==="CARD"&&p.ready)','second card load');
    const second=await evaluate(inspect);
    console.log('Reload:',JSON.stringify(second));
    assert.equal(second.probe.reach,7,'Progress did not reload');
    assert.equal(second.probe.generation,0,'Reload boot rewrote the file instead of reading it');
    assert.equal(second.committed,true);assert.equal(second.bridge.persisted,true);
    fs.writeFileSync(path.join(out,'result.json'),JSON.stringify({first,second},null,2));
    console.log('PASS: slot-1 card write, IndexedDB flush, full emulator reload and save-file read.');
  } finally {
    clearTimeout(watchdog);
    fs.writeFileSync(path.join(out,'browser.log'),logs.join('\n'));
    chrome.kill();server.close();
  }
}
run().catch(error=>{console.error(error);process.exitCode=1;server.close();});
