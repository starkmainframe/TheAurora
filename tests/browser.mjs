const {chromium}=await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
import fs from 'node:fs';
import assert from 'node:assert/strict';
const root=new URL('../',import.meta.url).pathname;
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
 const file=root+'web/'+url.pathname.replace(/^\/TheAurora\//,'');
 const path=file.endsWith('/')?file+'index.html':file;
 if(!fs.existsSync(path))return route.fulfill({status:404,body:'missing'});
 const type=path.endsWith('.js')?'text/javascript':path.endsWith('.css')?'text/css':path.endsWith('.png')?'image/png':path.endsWith('.json')?'application/json':'text/html';
 return route.fulfill({contentType:type,body:fs.readFileSync(path)});
});
await page.goto('https://starkmainframe.github.io/TheAurora/flash/');
await page.evaluate(()=>customElements.whenDefined('esp-web-install-button'));
assert.equal(await page.locator('#installer').isVisible(),false);
for(const board of ['143','175']) {await page.selectOption('#board',board);assert.equal(await page.locator('#installer').getAttribute('manifest'),`manifest-${board}.json`);assert.equal(await page.locator('#installer').isVisible(),true)}
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
console.log('PASS: desktop/mobile flasher, both board selections, config save and full HTTPS phone-location roundtrip with mocked device API');
await browser.close();
