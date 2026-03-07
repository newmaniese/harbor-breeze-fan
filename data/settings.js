(function () {
  var statusEl = document.getElementById('settings-status');
  var txInvertEl = document.getElementById('tx-invert');
  var syncLightEl = document.getElementById('sync-light');
  var syncFanSpeedEl = document.getElementById('sync-fan-speed');
  var syncFanDirEl = document.getElementById('sync-fan-direction');
  var syncSaveBtn = document.getElementById('sync-state-save');

  function setStatus(msg, className) {
    if (!statusEl) return;
    statusEl.textContent = msg;
    statusEl.className = 'status' + (className ? ' ' + className : '');
  }

  // Load transmitter settings
  fetch('/settings')
    .then(function (r) { return r.ok ? r.json() : null; })
    .then(function (d) {
      if (d && typeof d.tx_invert !== 'undefined' && txInvertEl) txInvertEl.value = String(d.tx_invert);
    })
    .catch(function () {});

  // Load current state into sync dropdowns
  fetch('/api/state')
    .then(function (r) { return r.ok ? r.json() : null; })
    .then(function (d) {
      if (!d) return;
      if (syncLightEl) syncLightEl.value = d.light_on ? '1' : '0';
      if (syncFanSpeedEl) syncFanSpeedEl.value = String(d.fan_speed);
      if (syncFanDirEl) syncFanDirEl.value = d.fan_direction === 'winter' ? 'winter' : 'summer';
    })
    .catch(function () {});

  if (syncSaveBtn) {
    syncSaveBtn.addEventListener('click', function () {
      var lightOn = syncLightEl && syncLightEl.value === '1';
      var speed = syncFanSpeedEl ? parseInt(syncFanSpeedEl.value, 10) : 0;
      var dir = syncFanDirEl ? syncFanDirEl.value : 'summer';
      if (isNaN(speed) || speed < 0 || speed > 6) speed = 0;
      setStatus('Saving…');
      fetch('/api/state', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ light_on: lightOn, fan_speed: speed, fan_direction: dir })
      })
        .then(function (r) { return r.json(); })
        .then(function (data) {
          if (data && data.ok) setStatus('State saved.', 'sent');
          else setStatus('Failed to save', 'error');
        })
        .catch(function () { setStatus('Request failed', 'error'); });
    });
  }

  if (txInvertEl) {
    txInvertEl.addEventListener('change', function () {
      var v = parseInt(this.value, 10);
      if (isNaN(v) || (v !== 0 && v !== 1)) return;
      setStatus('Saving…');
      fetch('/settings', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ tx_invert: v })
      })
        .then(function (r) { return r.json(); })
        .then(function (data) {
          if (data && data.ok) setStatus('Saved. TX invert = ' + data.tx_invert, 'sent');
          else setStatus('Failed to save', 'error');
        })
        .catch(function () { setStatus('Request failed', 'error'); });
    });
  }
})();
