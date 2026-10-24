const {chromium}=await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
import fs from 'node:fs';
import assert from 'node:assert/strict';
import {resolve} from 'node:path';
const root=new URL('../',import.meta.url).pathname;
const pages=resolve(process.argv[2] || root+'web');
const browser=await chromium.launch({executablePath:process.env.CHROME_PATH || undefined,headless:true,args:['--no-sandbox']});
const context=await browser.newContext({viewport:{width:1280,height:960},geolocation:{latitude:53.6,longitude:9.82}});
await context.grantPermissions(['geolocation'],{origin:'https://starkmainframe.github.io'});
const page=await context.newPage();
const errors=[];page.on('pageerror',error=>errors.push(error.message));
const config={latitude:53.6,longitude:9.82,brightness:150,pollMinutes:15,ssid:'Home',token:'a'.repeat(32),board:'ESP32-S3-Touch-AMOLED-1.43',status:'WiFi: 192.168.1.5'};
const writes=[];
await page.route('**/*',async route=>{
 const request=route.request(),url=new URL(request.url());
 if(url.hostname==='unpkg.com')return route.continue();
 if(url.hostname==='theaurora.local'){
  if(url.pathname==='/api/config')return route.fulfill({json:config});
  if(request.method()==='POST'){writes.push({path:url.pathname,data:new URLSearchParams(request.postData())});return route.fulfill({json:{message:'Saved on your display.'}})}
  return route.fulfill({contentType:'text/html',body:fs.readFileSync(root+'web/config.html','utf8')});
 }
 const file=pages+'/'+url.pathname.replace(/^\/TheAurora\//,'');
 const path=file.endsWith('/')?file+'index.html':file;
 if(!fs.existsSync(path))return route.fulfill({status:404,body:'missing'});
 const type=path.endsWith('.js')?'text/javascript':path.endsWith('.css')?'text/css':path.endsWith('.png')?'image/png':path.endsWith('.json')?'application/json':'text/html';
 return route.fulfill({contentType:type,body:fs.readFileSync(path)});
});
await page.goto('https://starkmainframe.github.io/TheAurora/flash/');
await page.waitForFunction(()=>customElements.get('esp-web-install-button'));
assert.equal(await page.locator('#installer').isVisible(),false);
assert.deepEqual(await page.locator('#board option').evaluateAll(options=>options.map(option=>option.value)),['','143','175']);
for(const width of [320,390,700,701,768,1280,1920]) {
 await page.setViewportSize({width,height:960});
 for(const board of ['143','175']) {
  await page.selectOption('#board',board);
  assert.equal(await page.locator('#installer').getAttribute('manifest'),`manifest-${board}.json`);
  assert.equal(await page.locator('#installer').isVisible(),true);
  const globe=await page.locator('.globe').evaluate(image=>{
   const box=image.getBoundingClientRect(),style=getComputedStyle(image);
   return {width:box.width,height:box.height,left:box.left,right:box.right,radius:style.borderRadius,
    ratio:style.aspectRatio,fit:style.objectFit,naturalWidth:image.naturalWidth,naturalHeight:image.naturalHeight};
  });
  assert.ok(globe.width>0 && Math.abs(globe.width-globe.height)<1,`${board} at ${width}px: preview is ${globe.width} × ${globe.height}`);
  assert.equal(globe.radius,'50%');assert.equal(globe.ratio,'1 / 1');assert.equal(globe.fit,'contain');
  assert.equal(globe.naturalWidth,466);assert.equal(globe.naturalHeight,466);
  assert.ok(globe.left>=0 && globe.right<=width,`${board} at ${width}px: preview overflows`);
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
 }
}
await page.selectOption('#board','');
assert.equal(await page.locator('#installer').isVisible(),false);
assert.equal(await page.locator('#installer').getAttribute('manifest'),null);
await page.setViewportSize({width:1280,height:960});
await page.screenshot({path:root+'build/flasher-desktop.png',fullPage:true});
await page.setViewportSize({width:390,height:844});await page.screenshot({path:root+'build/flasher-phone.png',fullPage:true});
assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
await page.goto('http://theaurora.local/');await page.waitForFunction(()=>document.getElementById('latitude').value==='53.6');
await page.fill('#latitude','60.125');await page.fill('#longitude','-0.5');await page.locator('#settings button[type=submit]').click();
await page.waitForFunction(()=>document.getElementById('message').textContent==='Saved on your display.');
assert.equal(writes.at(-1).data.get('latitude'),'60.125');assert.equal(writes.at(-1).data.get('token'),config.token);
await page.screenshot({path:root+'build/config-phone.png',fullPage:true});
await page.locator('#locate').click();await page.waitForURL('https://starkmainframe.github.io/**');
await page.locator('#locate').click();await page.locator('#return').waitFor({state:'visible'});
await page.locator('#return').click();await page.waitForURL('http://theaurora.local/');
await page.waitForFunction(()=>document.getElementById('message').textContent==='Saved on your display.');
assert.equal(writes.at(-1).data.get('latitude'),'53.60000');assert.equal(writes.at(-1).data.get('longitude'),'9.82000');
assert.equal(page.url(),'http://theaurora.local/');assert.deepEqual(errors,[]);
console.log('PASS: round 466×466 preview at 320–1920px for both boards, ESP Web Tools, board selection/reset, config save and HTTPS phone-location roundtrip');
await browser.close();
