export function deviceReturn(raw) {
  const url = new URL(raw || 'http://theaurora.local/');
  const localIP = /^(10\.(\d{1,3}\.){2}\d{1,3}|192\.168\.\d{1,3}\.\d{1,3}|172\.(1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3})$/;
  const isPrivate = localIP.test(url.hostname) && url.hostname.split('.').every(n => Number(n) <= 255);
  if (url.protocol !== 'http:' || url.username || url.password || url.port ||
      (url.hostname !== 'theaurora.local' && !isPrivate)) throw Error('Open this helper from your display’s settings page.');
  return new URL('/', url.origin);
}
export function locationReturn(raw, state, latitude, longitude) {
  if (!/^[a-f0-9]{32}$/.test(state || '')) throw Error('Location request expired. Open it again from your display.');
  if (!Number.isFinite(latitude) || !Number.isFinite(longitude) || latitude < 0 || latitude > 90 || longitude < -180 || longitude > 180)
    throw Error('TheAurora v1 supports northern latitudes (0–90°) only.');
  const url = deviceReturn(raw);
  url.hash = new URLSearchParams({lat:latitude.toFixed(5),lon:longitude.toFixed(5),state}).toString();
  return url.href;
}
if (typeof document !== 'undefined') {
  const parameters = new URLSearchParams(location.hash.slice(1));
  history.replaceState(null,'',location.pathname);
  const status = document.getElementById('status');
  document.getElementById('locate').onclick = () => {
    if (!navigator.geolocation) { status.textContent='Location is unavailable in this browser. Enter coordinates manually on the display’s settings page.'; return; }
    status.textContent='Waiting for location permission…';
    navigator.geolocation.getCurrentPosition(position => {
      const {latitude,longitude} = position.coords;
      document.getElementById('coordinates').textContent=`Latitude ${latitude.toFixed(5)} · Longitude ${longitude.toFixed(5)}`;
      document.getElementById('manual').hidden=false;
      try {
        const link=document.getElementById('return');
        link.href=locationReturn(parameters.get('return'),parameters.get('state'),latitude,longitude);
        link.hidden=false; status.textContent='Location found. Tap “Save on my display” to return and save it.';
      } catch(error) { status.textContent=error.message; }
    }, error => { status.textContent='Could not get your location: '+error.message+'. You can enter coordinates manually.'; },
    {timeout:20000,maximumAge:60000,enableHighAccuracy:false});
  };
}
