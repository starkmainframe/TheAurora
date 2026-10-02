import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {manifestFor} from '../web/flash/flasher.js';
import {deviceReturn,locationReturn} from '../web/flash/location.js';
assert.equal(manifestFor('143'),'manifest-143.json');
assert.equal(manifestFor('175'),'manifest-175.json');
for(const value of ['', '999', '__proto__', 'constructor']) assert.equal(manifestFor(value),null);
for(const board of ['143','175']) {
  const manifest=JSON.parse(readFileSync(new URL('../web/flash/'+manifestFor(board),import.meta.url)));
  assert.equal(manifest.builds[0].chipFamily,'ESP32-S3');
  assert.equal(manifest.builds[0].parts[0].path,`firmware-${board}.bin`);
  assert.equal(manifest.builds[0].parts[0].offset,0);
}
for(const host of ['http://theaurora.local/','http://192.168.4.1/','http://10.0.1.2/','http://172.16.1.2/']) assert.equal(deviceReturn(host).origin,new URL(host).origin);
for(const host of ['https://evil.com/','http://evil.com/','javascript:alert(1)','http://192.168.999.1/','http://192.168.1.1.evil.com/','http://theaurora.local@evil.com/','http://172.32.0.1/','http://127.0.0.1/','http://theaurora.local:8080/']) assert.throws(()=>deviceReturn(host));
const token='a'.repeat(32);
const url=new URL(locationReturn('http://192.168.1.5/',token,53.6,9.82));
assert.equal(url.search,''); // coordinates and token stay in fragment, never reach Pages logs
assert.equal(new URLSearchParams(url.hash.slice(1)).get('lat'),'53.60000');
assert.equal(new URLSearchParams(url.hash.slice(1)).get('state'),token);
for(const [lat,lon] of [[-1,0],[91,0],[0,181],[NaN,0],[0,Infinity]]) assert.throws(()=>locationReturn(null,token,lat,lon));
assert.throws(()=>locationReturn(null,'expired',53,9));
console.log('PASS: board/manifest mapping, local location callback allowlist, token and coordinate validation');
