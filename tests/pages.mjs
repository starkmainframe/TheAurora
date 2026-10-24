// Regression cases must fail before an incomplete or corrupt Pages tree can be uploaded.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {cpSync,mkdtempSync,readFileSync,rmSync,writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';

// Playwright 1.59.1 has no Chromium build for the image behind ubuntu-latest once that label moves.
function jobsIn(yaml) {
  const jobs=[];
  let current=null;
  let inJobs=false;
  for(const line of yaml.split(/\r?\n/)) {
    if(!inJobs) { if(line==='jobs:') inJobs=true; continue; }
    if(line && !/^[\s#]/.test(line)) { inJobs=false; current=null; continue; }
    const name=line.match(/^  ([A-Za-z0-9_-]+):\s*$/);
    if(name) { current={name:name[1],runsOn:null,body:''}; jobs.push(current); continue; }
    if(!current) continue;
    current.body+=line+'\n';
    const runsOn=line.match(/^    runs-on:\s*['"]?([^'"\s#]+)['"]?\s*(?:#.*)?$/);
    if(runsOn) current.runsOn=runsOn[1];
  }
  return jobs;
}
function playwrightRunnerIssues(yaml) {
  return jobsIn(yaml).filter(job=>job.body.includes('playwright install') && (!job.runsOn || job.runsOn.endsWith('-latest'))).map(job=>`${job.name}: playwright install requires a concrete runs-on`);
}
const workflow=readFileSync(new URL('../.github/workflows/webflasher.yml',import.meta.url),'utf8');
const playwrightJob=jobsIn(workflow).find(job=>job.body.includes('playwright install'));
assert.equal(playwrightJob?.runsOn,'ubuntu-24.04');
assert.deepEqual(playwrightRunnerIssues(workflow),[]);
assert.deepEqual(playwrightRunnerIssues(workflow.replace('runs-on: ubuntu-24.04','runs-on: ubuntu-latest')),['build: playwright install requires a concrete runs-on']);
const floatingSample='jobs:\n  build:\n    runs-on: ubuntu-latest\n    steps:\n      - run: playwright install chromium\n';
assert.deepEqual(playwrightRunnerIssues(floatingSample),['build: playwright install requires a concrete runs-on']);
assert.deepEqual(playwrightRunnerIssues(floatingSample.replace('ubuntu-latest','ubuntu-24.04')),[]);

const source=resolve(process.argv[2] || 'web');
const temp=mkdtempSync(join(tmpdir(),'theaurora-pages-'));
const pages=join(temp,'pages');
function check() {
  return spawnSync(process.execPath,['tools/verify_pages.mjs',pages],{encoding:'utf8'});
}
function editJSON(path,change) {
  const value=JSON.parse(readFileSync(path));change(value);writeFileSync(path,JSON.stringify(value));
}
function replaceText(file,from,to) {
  const before=readFileSync(file,'utf8');
  const after=before.replace(from,to);
  if(after===before) throw new Error('mutation did not apply');
  writeFileSync(file,after);
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
  ['wrong board mapping',dir=>replaceText(join(dir,'flasher.js'),"'143':'manifest-143.json'","'143':'manifest-175.json'"),/Wrong board manifest/],
  ['wrong flash mode',dir=>{
    const file=join(dir,'firmware-143.bin');
    const bytes=readFileSync(file);
    const before=bytes[2];
    // qio_opi must boot DIO (byte 2). Force the rejected QIO byte unless it is already there.
    bytes[2]=before===0?1:0;
    if(bytes[2]===before) throw new Error('mutation did not apply');
    writeFileSync(file,bytes);
    const metaPath=join(dir,'build.json');
    const meta=JSON.parse(readFileSync(metaPath,'utf8'));
    meta.boards['143'].sha256=createHash('sha256').update(bytes).digest('hex');
    meta.boards['143'].bootFlashMode=bytes[2];
    writeFileSync(metaPath,JSON.stringify(meta));
  },/flash mode/],
];
try {
  cpSync(source,pages,{recursive:true});
  assert.throws(()=>replaceText(join(pages,'flash','index.html'),'__missing_mutation__','x'),{message:'mutation did not apply'});
  const valid=check();assert.equal(valid.status,0,valid.stdout+valid.stderr);
  rmSync(pages,{recursive:true,force:true});cpSync(source,pages,{recursive:true});
  const htmlFile=join(pages,'flash','index.html');
  replaceText(htmlFile,'<script type="module" src="https://unpkg.com/esp-web-tools@10.1.0/dist/web/install-button.js?module">','<script src="https://unpkg.com/esp-web-tools@10.1.0/dist/web/install-button.js?module" type="module">');
  replaceText(htmlFile,'<esp-web-install-button id="installer" hidden>','<esp-web-install-button hidden id="installer">');
  const reordered=check();assert.equal(reordered.status,0,reordered.stdout+reordered.stderr);
  for(const [name,mutate,expected] of cases) {
    rmSync(pages,{recursive:true,force:true});cpSync(source,pages,{recursive:true});
    mutate(join(pages,'flash'));
    const result=check();
    assert.notEqual(result.status,0,`${name}: invalid artifact was accepted`);
    assert.match(result.stderr,expected,`${name}: unexpected failure`);
  }
  console.log(`PASS: valid Pages artifact accepted; reordered installer attributes accepted; ${cases.length} broken artifacts rejected; Playwright runner pinned to ubuntu-24.04`);
} finally {rmSync(temp,{recursive:true,force:true})}
