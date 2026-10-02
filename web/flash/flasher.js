export const manifests = Object.freeze({'143':'manifest-143.json','175':'manifest-175.json'});
export function manifestFor(board) { return Object.prototype.hasOwnProperty.call(manifests,board) ? manifests[board] : null; }
if (typeof document !== 'undefined') {
  const board=document.getElementById('board'), installer=document.getElementById('installer');
  board.onchange=()=>{
    const manifest=manifestFor(board.value);
    installer.hidden=!manifest;
    if(manifest) installer.setAttribute('manifest',manifest); else installer.removeAttribute('manifest');
    document.getElementById('selection').textContent=manifest ? `Selected: Waveshare ${board.options[board.selectedIndex].text}. Verify the board label before installing.` : 'Select the exact board before connecting.';
  };
  fetch('build.json',{cache:'no-store'}).then(r=>{if(!r.ok)throw Error();return r.json()}).then(build=>{
    document.getElementById('build').textContent=`Firmware ${build.version} · commit ${build.commit.slice(0,8)}`;
  }).catch(()=>{document.getElementById('build').textContent='Firmware is published by a successful GitHub Actions build.'});
}
