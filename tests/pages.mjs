// Regression cases must fail before an incomplete or corrupt Pages tree can be uploaded.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {cpSync,mkdtempSync,readFileSync,rmSync,writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';

const source=resolve(process.argv[2] || 'web');
const temp=mkdtempSync(join(tmpdir(),'theaurora-pages-'));
const pages=join(temp,'pages');
function check() {
  return spawnSync(process.execPath,['tools/verify_pages.mjs',pages],{encoding:'utf8'});
}
function editJSON(path,change) {
  const value=JSON.parse(readFileSync(path));change(value);writeFileSync(path,JSON.stringify(value));
}
const cases=[
  ['missing firmware',dir=>rmSync(join(dir,'firmware-143.bin')),/ENOENT/],
  ['empty firmware',dir=>writeFileSync(join(dir,'firmware-175.bin'),''),/Empty asset/],
  ['invalid manifest JSON',dir=>writeFileSync(join(dir,'manifest-143.json'),'{'),/SyntaxError/],
  ['wrong board binary',dir=>editJSON(join(dir,'manifest-143.json'),m=>m.builds[0].parts[0].path='firmware-175.bin'),/Wrong flash layout/],
  ['wrong offset',dir=>editJSON(join(dir,'manifest-175.json'),m=>m.builds[0].parts[0].offset=0x10000),/Wrong flash layout/],
  ['wrong chip',dir=>editJSON(join(dir,'manifest-143.json'),m=>m.builds[0].chipFamily='ESP32'),/Wrong flash layout/],
  ['root-relative binary',dir=>editJSON(join(dir,'manifest-143.json'),m=>m.builds[0].parts[0].path='/firmware-143.bin'),/Wrong flash layout/],
  ['corrupt binary',dir=>{const file=join(dir,'firmware-175.bin'),bytes=readFileSync(file);bytes[0x10000]^=1;writeFileSync(file,bytes)},/SHA-256 mismatch/],
  ['missing metadata',dir=>rmSync(join(dir,'build.json')),/ENOENT/],
  ['missing module',dir=>rmSync(join(dir,'flasher.js')),/ENOENT/],
  ['missing preview',dir=>rmSync(join(dir,'globe.png')),/ENOENT/],
  ['wrong board mapping',dir=>{const file=join(dir,'flasher.js');writeFileSync(file,readFileSync(file,'utf8').replace("'143':'manifest-143.json'","'143':'manifest-175.json'"))},/Wrong board manifest/],
];
try {
  cpSync(source,pages,{recursive:true});
  const valid=check();assert.equal(valid.status,0,valid.stdout+valid.stderr);
  for(const [name,mutate,expected] of cases) {
    rmSync(pages,{recursive:true,force:true});cpSync(source,pages,{recursive:true});
    mutate(join(pages,'flash'));
    const result=check();
    assert.notEqual(result.status,0,`${name}: invalid artifact was accepted`);
    assert.match(result.stderr,expected,`${name}: unexpected failure`);
  }
  console.log(`PASS: valid Pages artifact accepted; ${cases.length} broken artifacts rejected`);
} finally {rmSync(temp,{recursive:true,force:true})}
