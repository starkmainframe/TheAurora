#!/usr/bin/env node
// Validate the exact directory uploaded by actions/upload-pages-artifact.
// Usage: node tools/verify_pages.mjs [web or a staged Pages directory]
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {readFileSync,statSync} from 'node:fs';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';

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
assert.match(html,/<script\b[^>]*type="module"[^>]*src="https:\/\/unpkg\.com\/esp-web-tools@10\.1\.0\/dist\/web\/install-button\.js\?module"/,'Missing pinned ESP Web Tools module');
assert.match(html,/<esp-web-install-button\b[^>]*id="installer"[^>]*hidden/,'Installer must start hidden');
assert.match(html,/<button\b[^>]*slot="activate"/,'Missing install activation button');
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
  assert.ok(image.length>0x10000+24 && image.length<=16*1024*1024,'Invalid merged image size');
  for(const offset of [0,0x10000]) {
    assert.equal(image[offset],0xe9,`Missing ESP image at ${offset}`);
    assert.equal(image.readUInt16LE(offset+12),9,'Image is not ESP32-S3');
  }
  assert.equal(image[2],2,'Merged bootloader must use DIO');
  assert.equal(image[3],0x4f,'Merged bootloader must use 16 MB / 80 MHz');
  assert.equal(image.readUInt16LE(0x8000),0x50aa,'Missing partition table at 0x8000');
  let app;
  for(let offset=0x8000;offset<0x9000 && image.readUInt16LE(offset)===0x50aa;offset+=32) {
    if(image[offset+2]===0 && image[offset+3]===0x10) app={offset:image.readUInt32LE(offset+4),size:image.readUInt32LE(offset+8)};
  }
  assert.ok(app,'Missing OTA application partition');
  assert.equal(app.offset,0x10000,'Application partition must start at 0x10000');
  assert.ok(image.length<=app.offset+app.size,'Application exceeds its partition');
  assert.equal(image.readUInt32LE(0xe000),1,'Missing boot_app0 OTA data at 0xe000');
  console.log(`PASS: ${board} → ${path} → ${part.path} at offset 0 (${image.length} bytes, SHA-256 verified)`);
}
assert.notEqual(metadata.boards['143'].sha256,metadata.boards['175'].sha256,'Both boards must have their own firmware');
const preview=asset('globe.png');
assert.equal(preview.subarray(0,8).toString('hex'),'89504e470d0a1a0a','Invalid PNG preview');
assert.equal(preview.readUInt32BE(16),466);assert.equal(preview.readUInt32BE(20),466);
console.log(`PASS: Pages assets, board picker, ESP Web Tools inclusion and 466×466 preview in ${pages.pathname}`);
