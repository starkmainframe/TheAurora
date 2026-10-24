#!/usr/bin/env node
// Validate the exact directory uploaded by actions/upload-pages-artifact.
// Usage: node tools/verify_pages.mjs [web or a staged Pages directory]
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {readFileSync,statSync} from 'node:fs';
import {resolve} from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';

// Same ESP32-S3 header tables as tools/package_firmware.py and esptool.
const FLASH_MODE_BYTE={qio:0,qout:1,dio:2,dout:3};
const FLASH_SIZE_HIGH={'1MB':0x00,'2MB':0x10,'4MB':0x20,'8MB':0x30,'16MB':0x40,'32MB':0x50,'64MB':0x60,'128MB':0x70};
const FLASH_FREQ_LOW={'80m':0x0f,'40m':0x00,'26m':0x01,'20m':0x02};
const DEFAULT_FLASH_FREQ='80m';
const FLASH_BYTES=16*1024*1024;
function iniSections(text) {
  const sections=new Map();
  let current=null;
  for(const raw of text.split(/\r?\n/)) {
    const line=raw.split(';',1)[0].trim();
    if(!line || line.startsWith('#')) continue;
    if(line.startsWith('[') && line.endsWith(']')) { current=line.slice(1,-1).trim(); if(!sections.has(current)) sections.set(current,new Map()); continue; }
    if(!current || !line.includes('=')) continue;
    const eq=line.indexOf('=');
    sections.get(current).set(line.slice(0,eq).trim(),line.slice(eq+1).trim().replace(/^['"]|['"]$/g,''));
  }
  return sections;
}
function lookup(sections,section,key) {
  const seen=new Set();
  const queue=[section];
  while(queue.length) {
    const name=queue.shift();
    if(seen.has(name)) continue;
    seen.add(name);
    const body=sections.get(name);
    assert.ok(body,`platformio.ini has no [${name}]`);
    if(body.has(key)) return body.get(key);
    for(const parent of (body.get('extends') || '').split(',')) if(parent.trim()) queue.push(parent.trim());
  }
  return null;
}
function freqToken(value) {
  if(!value) return DEFAULT_FLASH_FREQ;
  if(/^\d+m$/i.test(value)) return value.toLowerCase();
  const hz=Number(String(value).replace(/L$/i,''));
  assert.ok(Number.isFinite(hz),`Unsupported flash frequency ${value}`);
  return `${hz/1e6}m`;
}
function projectFlash(iniText) {
  const sections=iniSections(iniText);
  const resolved=[];
  for(const env of ['env:esp32-s3-amoled-143','env:esp32-s3-amoled-175']) {
    const mode=lookup(sections,env,'board_build.flash_mode');
    const size=lookup(sections,env,'board_upload.flash_size');
    const freq=freqToken(lookup(sections,env,'board_build.f_flash') || lookup(sections,env,'board_build.f_image'));
    assert.ok(Object.hasOwn(FLASH_MODE_BYTE,mode),`Unsupported flash mode ${mode}`);
    assert.ok(Object.hasOwn(FLASH_SIZE_HIGH,size),`Unsupported flash size ${size}`);
    assert.ok(Object.hasOwn(FLASH_FREQ_LOW,freq),`Unsupported flash frequency ${freq}`);
    resolved.push({mode,freq,size,modeByte:FLASH_MODE_BYTE[mode],sizeFreqByte:FLASH_SIZE_HIGH[size]|FLASH_FREQ_LOW[freq]});
  }
  assert.deepEqual(resolved[0],resolved[1],'Board flash settings differ');
  return resolved[0];
}
const repoIni=readFileSync(fileURLToPath(new URL('../platformio.ini',import.meta.url)),'utf8');
const projectFlashSettings=projectFlash(repoIni);

const pages=pathToFileURL(resolve(process.argv[2] || 'web')+'/');
const flash=new URL('flash/',pages);
const boards=['143','175'];
function asset(path,base=flash) {
  assert.equal(typeof path,'string','Asset path must be a string');
  assert.ok(path && !path.startsWith('/') && !path.includes('\\') && !/^[a-z]+:/i.test(path),`Not a relative Pages path: ${path}`);
  const url=new URL(path,base);
  assert.ok(url.href.startsWith(pages.href) && !url.search && !url.hash,`Asset escapes Pages: ${path}`);
  assert.ok(statSync(url).isFile(),`Not a file: ${path}`);
  const bytes=readFileSync(url);
  assert.ok(bytes.length>0,`Empty asset: ${path}`);
  return bytes;
}
const html=asset('index.html').toString();
function attributes(body) {
  const attrs=new Map();
  for(const match of body.matchAll(/([^\s=\/]+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'=<>`]+)))?/g)) attrs.set(match[1],match[2] ?? match[3] ?? match[4] ?? '');
  return attrs;
}
function tags(name) {
  return [...html.matchAll(new RegExp(`<${name}\\b([^>]*)>`,'g'))].map(match=>attributes(match[1]));
}
const espTools='https://unpkg.com/esp-web-tools@10.1.0/dist/web/install-button.js?module';
assert.ok(tags('script').some(attrs=>attrs.get('type')==='module' && attrs.get('src')===espTools),'Missing pinned ESP Web Tools module');
assert.ok(tags('esp-web-install-button').some(attrs=>attrs.get('id')==='installer' && attrs.has('hidden')),'Installer must start hidden');
assert.ok(tags('button').some(attrs=>attrs.get('slot')==='activate'),'Missing install activation button');
// Check the actual local script, stylesheet and image references, not just fixed filenames.
for(const [tag] of html.matchAll(/<(?:script|link|img)\b[^>]*>/g)) {
  const path=tag.match(/(?:src|href)="([^"]+)"/)?.[1];
  if(path && !path.startsWith('https://')) asset(path);
}
const options=html.match(/<select\b[^>]*id="board"[^>]*>([\s\S]*?)<\/select>/)?.[1];
assert.ok(options,'Missing board picker');
assert.deepEqual([...options.matchAll(/<option\b[^>]*value="([^"]*)"/g)].map(match=>match[1]),['',...boards]);
asset('flasher.js');
const {manifests,manifestFor}=await import(new URL('flasher.js',flash));
assert.deepEqual(Object.keys(manifests).sort(),boards,'Unexpected board mapping');
for(const invalid of ['', '999', '__proto__', 'constructor']) assert.equal(manifestFor(invalid),null);
const metadata=JSON.parse(asset('build.json'));
assert.ok(typeof metadata.version==='string' && metadata.version.length>0,'Missing version');
assert.match(metadata.commit,/^[a-f0-9]{40}$/,'Invalid build commit');
assert.deepEqual(Object.keys(metadata.boards).sort(),boards,'Missing board metadata');
for(const board of boards) {
  const path=manifestFor(board);
  assert.equal(path,`manifest-${board}.json`,'Wrong board manifest');
  const manifest=JSON.parse(asset(path));
  assert.ok(manifest.name.includes(board==='143'?'1.43':'1.75'),'Wrong board name');
  assert.equal(manifest.version,metadata.version,'Manifest/build version mismatch');
  assert.equal(manifest.new_install_prompt_erase,true);
  assert.deepEqual(manifest.builds,[{chipFamily:'ESP32-S3',parts:[{path:`firmware-${board}.bin`,offset:0}]}],`Wrong flash layout for ${board}`);
  const part=manifest.builds[0].parts[0];
  const image=asset(part.path,new URL(path,flash)); // ESP Web Tools resolves paths relative to the manifest.
  const built=metadata.boards[board];
  assert.equal(built.file,part.path,'Metadata points to another board');
  assert.equal(image.length,built.bytes,'Firmware size mismatch');
  assert.equal(createHash('sha256').update(image).digest('hex'),built.sha256,'Firmware SHA-256 mismatch');
  assert.ok(image.length>0x10000+24 && image.length<=FLASH_BYTES,'Invalid merged image size');
  for(const offset of [0,0x10000]) {
    assert.equal(image[offset],0xe9,`Missing ESP image at ${offset}`);
    assert.equal(image.readUInt16LE(offset+12),9,'Image is not ESP32-S3');
  }
  assert.equal(metadata.flash?.mode,projectFlashSettings.mode,'build.json flash mode must match platformio.ini');
  assert.equal(metadata.flash.modeByte,projectFlashSettings.modeByte,'build.json flash mode byte must match platformio.ini');
  assert.equal(built.bootFlashMode,image[2],'Metadata boot flash mode byte does not match the image');
  assert.equal(image[2],projectFlashSettings.modeByte,`Merged bootloader flash mode byte ${image[2]} must match platformio.ini ${projectFlashSettings.mode} (${projectFlashSettings.modeByte})`);
  assert.equal(image[3],projectFlashSettings.sizeFreqByte,'Merged bootloader size/frequency must match platformio.ini');
  const appMode=image[0x10000+2];
  assert.ok(Object.values(FLASH_MODE_BYTE).includes(appMode),'Application flash mode byte is invalid');
  assert.equal(built.appFlashMode,appMode,'Metadata app flash mode byte does not match the image');
  assert.equal(image.readUInt16LE(0x8000),0x50aa,'Missing partition table at 0x8000');
  let app,littlefs,coredump;
  const spans=[];
  for(let offset=0x8000;offset<0x9000 && image.readUInt16LE(offset)===0x50aa;offset+=32) {
    const type=image[offset+2],subtype=image[offset+3];
    const partOff=image.readUInt32LE(offset+4),size=image.readUInt32LE(offset+8);
    const name=image.subarray(offset+12,offset+28).toString('utf8').replace(/\0+$/,'');
    const partEnd=partOff+size;
    assert.ok(size>0 && partEnd<=FLASH_BYTES,`${name || 'partition'} ends past 16 MB`);
    for(const span of spans) assert.ok(partEnd<=span.off || partOff>=span.end,`${name} overlaps ${span.name}`);
    spans.push({off:partOff,end:partEnd,name});
    if(type===0 && subtype===0x10 && !app) app={offset:partOff,size};
    if(name==='littlefs') littlefs={end:partEnd};
    if(type===1 && subtype===0x03) coredump={name,offset:partOff,size};
  }
  assert.ok(app,'Missing OTA application partition');
  assert.equal(app.offset,0x10000,'Application partition must start at 0x10000');
  assert.ok(image.length<=app.offset+app.size,'Application exceeds its partition');
  assert.ok(littlefs,'Missing littlefs partition');
  assert.equal(littlefs.end,0xFF0000,'littlefs must end where the coredump starts');
  assert.deepEqual(coredump,{name:'coredump',offset:0xFF0000,size:0x10000},'Missing 64 KB coredump at 0xFF0000');
  assert.equal(image.readUInt32LE(0xe000),1,'Missing boot_app0 OTA data at 0xe000');
  console.log(`PASS: ${board} → ${path} → ${part.path} at offset 0 (${image.length} bytes, SHA-256 verified, boot flash mode ${projectFlashSettings.mode} byte ${image[2]}, app flash mode byte ${appMode})`);
}
assert.notEqual(metadata.boards['143'].sha256,metadata.boards['175'].sha256,'Both boards must have their own firmware');
const preview=asset('globe.png');
assert.equal(preview.subarray(0,8).toString('hex'),'89504e470d0a1a0a','Invalid PNG preview');
assert.equal(preview.readUInt32BE(16),466);assert.equal(preview.readUInt32BE(20),466);
console.log(`PASS: Pages assets, board picker, ESP Web Tools inclusion and 466×466 preview in ${pages.pathname}`);
