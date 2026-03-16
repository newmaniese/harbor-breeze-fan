(function () {
  var statusEl = document.getElementById('status');
  var hubCommands = ['light_toggle', 'light_dim', 'fan_off', 'fan_speed_1', 'fan_speed_2', 'fan_speed_3', 'fan_speed_4', 'fan_speed_5', 'fan_speed_6', 'nature_breeze', 'fan_direction_summer', 'fan_direction_winter', 'home_shield'];
  var lastLightOn = false;

  fetch('/config').then(function (r) { return r.ok ? r.json() : null; }).then(function (d) {
    if (d && d.transceiver_only) {
      document.body.classList.add('transceiver-only');
      var hint = document.querySelector('.transceiver-only-hint');
      if (hint) hint.style.display = '';
    }
  }).catch(function () {});

  function setStatus(msg, className) {
    if (!statusEl) return;
    statusEl.textContent = msg;
    statusEl.className = 'status' + (className ? ' ' + className : '');
  }

  function sendCommand(cmd) {
    var useHub = hubCommands.indexOf(cmd) >= 0;
    var url = useHub ? ('/send-hub?cmd=' + encodeURIComponent(cmd)) : '/send';
    setStatus('Sending…');
    var opts = useHub ? { method: 'GET' } : { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ cmd: cmd }) };
    fetch(url, opts)
      .then(function (r) {
        if (!r.ok) return r.json().then(function (d) { throw new Error((d && d.error) ? d.error : r.status + ''); }).catch(function () { throw new Error(r.status + ''); });
        return r.json();
      })
      .then(function (data) {
        if (data && data.ok) {
          setStatus('Sent: ' + data.cmd, 'sent');
          refreshState();
        } else {
          var err = (data && data.error) ? data.error : 'unknown';
          setStatus('Error: ' + err, 'error');
        }
      })
      .catch(function (err) {
        setStatus('Request failed: ' + (err.message || ''), 'error');
      });
  }

  function refreshState() {
    fetch('/api/state')
      .then(function (r) { return r.ok ? r.json() : null; })
      .then(function (d) {
        if (!d) return;
        lastLightOn = !!d.light_on;
        var delayEl = document.getElementById('delay-countdown');
        var slider = document.getElementById('fan-speed-slider');
        var dirToggle = document.getElementById('direction-toggle');
        var dirGroup = dirToggle && dirToggle.closest('.direction-toggle-group');
        var lightIcon = document.getElementById('light-icon');
        if (slider) {
          slider.value = d.fan_speed;
          slider.disabled = false;
        }
        if (dirToggle) dirToggle.setAttribute('data-dir', d.fan_direction || 'summer');
        if (dirGroup) dirGroup.setAttribute('data-dir', d.fan_direction || 'summer');
        if (lightIcon) {
          lightIcon.classList.toggle('light-icon-on', lastLightOn);
          lightIcon.classList.toggle('light-icon-off', !lastLightOn);
        }
        if (delayEl) {
          if (d.delay_active && d.delay_remaining_sec > 0) {
            var m = Math.floor(d.delay_remaining_sec / 60);
            var s = d.delay_remaining_sec % 60;
            delayEl.textContent = 'Off in ' + m + ':' + (s < 10 ? '0' : '') + s;
            delayEl.className = 'delay-countdown delay-active';
          } else {
            delayEl.textContent = '';
            delayEl.className = 'delay-countdown';
          }
        }
      })
      .catch(function () {});
  }

  // Light button: tap = toggle, hold = dim (only when light is on).
  // - dimmingActive: interval callback does nothing after stopDim().
  // - AbortController: cancel in-flight fetches on release so ESP32 gets fewer requests.
  // - 600ms interval: fewer commands so less "tail" after release; still smooth for dimming.
  // - Max dims per hold: safety cap so we never flood even if pointerup never fires.
  var lightBtn = document.getElementById('btn-light');
  var dimInterval = null;
  var dimmingActive = false;
  var dimAbortController = null;
  var dimCount = 0;
  var DIM_INTERVAL_MS = 600;
  var DIM_MAX_PER_HOLD = 30;
  var lightPressTimer = null;
  var lightDidHold = false;
  if (lightBtn) {
    function startDim() {
      if (dimmingActive || !lastLightOn) return;
      dimmingActive = true;
      dimCount = 0;
      dimAbortController = new AbortController();
      lightBtn.classList.add('light-dimming');
      sendCommand('light_dim');
      dimCount++;
      dimInterval = setInterval(function () {
        if (!dimmingActive || dimCount >= DIM_MAX_PER_HOLD) return;
        var ac = dimAbortController;
        if (!ac) return;
        fetch('/send-hub?cmd=light_dim', { signal: ac.signal }).catch(function () {});
        dimCount++;
      }, DIM_INTERVAL_MS);
    }
    function stopDim() {
      dimmingActive = false;
      if (dimAbortController) {
        dimAbortController.abort();
        dimAbortController = null;
      }
      if (dimInterval) {
        clearInterval(dimInterval);
        dimInterval = null;
      }
      lightBtn.classList.remove('light-dimming');
    }
    function onPointerDown(e) {
      if (e.button !== 0 && e.pointerType === 'mouse') return;
      lightDidHold = false;
      lightPressTimer = setTimeout(function () {
        lightDidHold = true;
        if (lastLightOn) startDim();
      }, 400);
    }
    function onPointerUp(e) {
      if (e.button !== 0 && e.pointerType === 'mouse') return;
      clearTimeout(lightPressTimer);
      lightPressTimer = null;
      stopDim();
      if (!lightDidHold) sendCommand('light_toggle');
    }
    function onPointerLeaveOrCancel() {
      clearTimeout(lightPressTimer);
      lightPressTimer = null;
      stopDim();
    }
    lightBtn.addEventListener('pointerdown', onPointerDown, { passive: true });
    lightBtn.addEventListener('pointerup', onPointerUp, { passive: true });
    lightBtn.addEventListener('pointerleave', onPointerLeaveOrCancel, { passive: true });
    lightBtn.addEventListener('pointercancel', onPointerLeaveOrCancel, { passive: true });
    lightBtn.addEventListener('mousedown', function (e) { e.preventDefault(); });
    lightBtn.addEventListener('touchstart', function (e) { e.preventDefault(); }, { passive: false });
    lightBtn.addEventListener('touchend', function (e) {
      e.preventDefault();
      onPointerLeaveOrCancel();
    }, { passive: false });
    lightBtn.addEventListener('touchcancel', function (e) {
      e.preventDefault();
      onPointerLeaveOrCancel();
    }, { passive: false });
    lightBtn.addEventListener('click', function (e) { e.preventDefault(); });
    window.addEventListener('blur', onPointerLeaveOrCancel);
    document.addEventListener('visibilitychange', function () { if (document.hidden) onPointerLeaveOrCancel(); });
  }

  // Fan speed slider: 0 = off (left), 1-6 = speed. Send on change (release).
  var fanSlider = document.getElementById('fan-speed-slider');
  if (fanSlider) {
    fanSlider.addEventListener('change', function () {
      var v = parseInt(this.value, 10);
      if (v === 0) sendCommand('fan_off');
      else if (v >= 1 && v <= 6) sendCommand('fan_speed_' + v);
    });
  }

  // Direction toggle: long-press only (avoid accidental switch)
  var directionToggle = document.getElementById('direction-toggle');
  var longPressMs = 1200;
  var directionPressTimer = null;
  if (directionToggle) {
    function clearDirectionTimer() {
      if (directionPressTimer) {
        clearTimeout(directionPressTimer);
        directionPressTimer = null;
      }
    }
    function startDirectionPress(e) {
      e.preventDefault();
      clearDirectionTimer();
      directionPressTimer = setTimeout(function () {
        directionPressTimer = null;
        var current = directionToggle.getAttribute('data-dir') || 'summer';
        var cmd = current === 'summer' ? 'fan_direction_winter' : 'fan_direction_summer';
        sendCommand(cmd);
      }, longPressMs);
    }
    directionToggle.addEventListener('mousedown', startDirectionPress);
    directionToggle.addEventListener('touchstart', function (e) { startDirectionPress(e); }, { passive: false });
    directionToggle.addEventListener('mouseup', clearDirectionTimer);
    directionToggle.addEventListener('mouseleave', clearDirectionTimer);
    directionToggle.addEventListener('touchend', clearDirectionTimer);
    directionToggle.addEventListener('touchcancel', clearDirectionTimer);
    directionToggle.addEventListener('click', function (e) { e.preventDefault(); });
  }

  // Buttons with data-cmd (light and direction are custom; exclude them)
  document.querySelectorAll('[data-cmd]').forEach(function (btn) {
    if (btn.id === 'direction-toggle') return;
    btn.addEventListener('click', function () {
      var cmd = this.getAttribute('data-cmd');
      if (cmd) sendCommand(cmd);
    });
  });

  refreshState();
  setInterval(refreshState, 2000);
  setStatus('Ready');
})();
