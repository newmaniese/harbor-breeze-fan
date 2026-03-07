(function () {
  var statusEl = document.getElementById('status');
  var logEl = document.getElementById('send-log');
  var rfLogEl = document.getElementById('rf-log');
  var wsStatusEl = document.getElementById('ws-status');
  var maxLogLines = 50;
  var maxRfLogLines = 30;

  function setStatus(msg, className) {
    if (!statusEl) return;
    statusEl.textContent = msg;
    statusEl.className = 'status' + (className ? ' ' + className : '');
  }

  function setWsStatus(connected) {
    if (!wsStatusEl) return;
    wsStatusEl.textContent = connected ? 'Live' : 'Disconnected';
    wsStatusEl.className = 'ws-badge ' + (connected ? 'ws-connected' : 'ws-disconnected');
  }

  function timeStr() {
    var d = new Date();
    return d.toTimeString().slice(0, 8);
  }

  function appendLog(cmd, ok) {
    if (!logEl) return;
    var line = document.createElement('div');
    line.className = 'send-log-line' + (ok ? '' : ' send-log-line-error');
    line.textContent = timeStr() + ' — ' + (ok ? 'Sent: ' + cmd : 'Error: ' + cmd);
    logEl.appendChild(line);
    while (logEl.children.length > maxLogLines) logEl.removeChild(logEl.firstChild);
    logEl.scrollTop = logEl.scrollHeight;
  }

  function appendRfLog(entry) {
    if (!rfLogEl) return;
    var line = document.createElement('div');
    line.className = 'rf-log-entry ' + (entry.recognized ? 'rf-log-recognized' : 'rf-log-unknown');
    var raw = entry.pulses && entry.pulses.length ? entry.pulses.slice(0, 20).join(', ') + (entry.length > 20 ? '…' : '') : '';
    var msg = entry.recognized && entry.command
      ? 'Recognized: ' + entry.command
      : (entry.func8 ? 'Unrecognized (func ' + entry.func8 + ')' : 'Unrecognized');
    line.innerHTML = '<span class="rf-log-ts">' + timeStr() + '</span> ' +
      '<span class="rf-log-msg">' + msg + '</span> ' +
      '<span class="rf-log-meta">' + entry.length + ' pulses</span>' +
      (raw ? ' <span class="rf-log-raw" title="First 20 pulses (µs)">[' + raw + ']</span>' : '');
    rfLogEl.appendChild(line);
    while (rfLogEl.children.length > maxRfLogLines) rfLogEl.removeChild(rfLogEl.firstChild);
    rfLogEl.scrollTop = rfLogEl.scrollHeight;
  }

  // Hub protocol commands (same protocol that works with the fan). Others use legacy POST /send.
  var hubCommands = ['light_toggle', 'light_dim', 'fan_off', 'fan_speed_1', 'fan_speed_2', 'fan_speed_3', 'fan_speed_4', 'fan_speed_5', 'fan_speed_6', 'nature_breeze', 'fan_direction_summer', 'fan_direction_winter', 'home_shield'];

  function sendCommand(cmd) {
    setStatus('Sending…');
    var useHub = hubCommands.indexOf(cmd) >= 0;
    var url = useHub ? ('/send-hub?cmd=' + encodeURIComponent(cmd)) : '/send';
    var opts = useHub ? { method: 'GET' } : { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ cmd: cmd }) };
    fetch(url, opts)
      .then(function (r) {
        if (!r.ok) return r.json().then(function (d) { throw new Error((d && d.error) ? d.error : r.status + ''); }).catch(function () { throw new Error(r.status + ''); });
        return r.json();
      })
      .then(function (data) {
        if (data && data.ok) {
          setStatus('Sent: ' + data.cmd + (data.protocol === 'hub' ? ' (hub)' : ''), 'sent');
          appendLog(data.cmd, true);
        } else {
          var err = (data && data.error) ? data.error : 'unknown';
          setStatus('Error: ' + err, 'error');
          appendLog(err, false);
        }
      })
      .catch(function (err) {
        setStatus('Request failed: ' + (err.message || ''), 'error');
        appendLog('Request failed', false);
      });
  }

  var buttons = document.querySelectorAll('[data-cmd]');
  buttons.forEach(function (btn) {
    btn.addEventListener('click', function () {
      var cmd = this.getAttribute('data-cmd');
      if (cmd) sendCommand(cmd);
    });
  });

  (function initRfLive() {
    var lastSeq = 0;
    function appendIfNew(d) {
      if (d.event !== 'rf' || !d.length) return;
      if (d.seq !== lastSeq) {
        lastSeq = d.seq;
        appendRfLog({
          length: d.length || 0,
          pulses: d.pulses || [],
          recognized: !!d.recognized,
          command: d.command || null,
          func8: d.func8 || null
        });
      }
    }
    var wsUrl = 'ws://' + location.host + '/ws';
    try {
      var socket = new WebSocket(wsUrl);
      socket.onopen = function () { setWsStatus(true); };
      socket.onclose = function () {
        setWsStatus(false);
        setTimeout(initRfLive, 2000);
      };
      socket.onmessage = function (ev) {
        try {
          var d = JSON.parse(ev.data);
          appendIfNew(d);
        } catch (e) { /* ignore */ }
      };
    } catch (e) {
      setWsStatus(false);
      setTimeout(initRfLive, 2000);
    }
    // Fallback: poll when page has no WebSocket (e.g. WS failed)
    setInterval(function () {
      if (typeof socket !== 'undefined' && socket.readyState === WebSocket.OPEN) return;
      fetch('/last-rf-event')
        .then(function (r) { return r.ok ? r.json() : null; })
        .then(function (d) {
          if (d) { setWsStatus(true); appendIfNew(d); }
          else setWsStatus(false);
        })
        .catch(function () { setWsStatus(false); });
    }, 2000);
  })();

  setStatus('Ready — tap a button to send');
  window.appReady = true;

  // --- Debug & settings ---
  function fillCommandSelects() {
    fetch('/commands')
      .then(function (r) { return r.ok ? r.json() : null; })
      .then(function (d) {
        if (!d || !d.commands || !d.commands.length) return;
        var opts = d.commands.map(function (c) { return '<option value="' + c + '">' + c + '</option>'; }).join('');
        var verifyCmd = document.getElementById('verify-cmd');
        var debugPulsesCmd = document.getElementById('debug-pulses-cmd');
        if (verifyCmd) verifyCmd.innerHTML = opts;
        if (debugPulsesCmd) debugPulsesCmd.innerHTML = opts;
      })
      .catch(function () {});
  }
  fillCommandSelects();

  function loadSettings() {
    fetch('/settings')
      .then(function (r) { return r.ok ? r.json() : null; })
      .then(function (d) {
        if (d && typeof d.tx_invert !== 'undefined') {
          var sel = document.getElementById('tx-invert');
          if (sel) sel.value = String(d.tx_invert);
        }
      })
      .catch(function () {});
  }
  loadSettings();

  var txInvertEl = document.getElementById('tx-invert');
  if (txInvertEl) {
    txInvertEl.addEventListener('change', function () {
      var v = parseInt(this.value, 10);
      if (isNaN(v) || (v !== 0 && v !== 1)) return;
      fetch('/settings', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ tx_invert: v })
      })
        .then(function (r) { return r.json(); })
        .then(function (data) {
          if (data && data.ok) setStatus('TX invert set to ' + data.tx_invert, 'sent');
          else setStatus('Failed to save', 'error');
        })
        .catch(function () { setStatus('Request failed', 'error'); });
    });
  }

  function setResult(id, html, isError) {
    var el = document.getElementById(id);
    if (!el) return;
    el.innerHTML = html;
    el.className = 'debug-result' + (isError ? ' debug-result-error' : '');
  }

  document.getElementById('btn-verify-tx') && document.getElementById('btn-verify-tx').addEventListener('click', function () {
    var cmd = (document.getElementById('verify-cmd') && document.getElementById('verify-cmd').value) || 'light_toggle';
    setResult('verify-tx-result', 'Sending and capturing…');
    fetch('/verify-tx?cmd=' + encodeURIComponent(cmd))
      .then(function (r) { return r.json(); })
      .then(function (d) {
        if (!d) { setResult('verify-tx-result', 'No response', true); return; }
        var seen = d.tx_seen_by_receiver ? 'Yes' : 'No';
        var newCap = d.new_capture_during_test ? 'Yes' : 'No';
        var html = '<p><strong>TX seen by receiver:</strong> ' + seen + '</p>' +
          '<p>Expected length: ' + (d.expected_length || 0) + ', Captured length: ' + (d.captured_length || 0) + '</p>' +
          '<p>Seq before: ' + (d.seq_before ?? '') + ', Seq after: ' + (d.seq_after ?? '') + ', New capture during test: ' + newCap + '</p>';
        if (d.expected_sample && d.expected_sample.length) html += '<p class="debug-sample">Expected sample (µs): ' + d.expected_sample.slice(0, 10).join(', ') + ' …</p>';
        if (d.captured_sample && d.captured_sample.length) html += '<p class="debug-sample">Captured sample (µs): ' + d.captured_sample.slice(0, 10).join(', ') + ' …</p>';
        if (d.captured_sample && d.captured_sample.length && d.captured_sample[0] > 2000) {
          html += '<p class="debug-hint">Captured values are in ms range (e.g. 5000+ µs). Our TX uses ~380/770 µs. Receiver may not be seeing the transmitter (check wiring, antenna, power) or may output different timing.</p>';
        }
        setResult('verify-tx-result', html, !d.tx_seen_by_receiver && d.captured_length !== undefined);
      })
      .catch(function () { setResult('verify-tx-result', 'Request failed', true); });
  });

  document.getElementById('btn-compare-remote') && document.getElementById('btn-compare-remote').addEventListener('click', function () {
    setResult('compare-remote-result', 'Loading…');
    Promise.all([
      fetch('/last-rf').then(function (r) { return r.json(); }),
      fetch('/debug-pulses?cmd=light_toggle').then(function (r) { return r.json(); })
    ]).then(function (results) {
      var captured = results[0];
      var expected = results[1];
      if (!captured || !expected) {
        setResult('compare-remote-result', 'Failed to load one or both. Press the remote’s Light button, then try again.', true);
        return;
      }
      var capLen = captured.length || 0;
      var capPulses = captured.pulses || [];
      var expLen = expected.length || 0;
      var expPulses = expected.pulses || [];
      var skip = (capPulses[0] >= 60000) ? 1 : 0;
      var capShow = capPulses.slice(skip, skip + 40);
      var expShow = expPulses.slice(0, 40);
      var html = '<p><strong>Remote capture</strong> length: ' + capLen + (skip ? ' (skipped first value ' + capPulses[0] + ' as receiver artifact)' : '') + '</p>';
      html += '<p class="debug-sample"><strong>Captured (µs):</strong> ' + capShow.join(', ') + (capPulses.length > skip + 40 ? ' …' : '') + '</p>';
      html += '<p><strong>Our expected (light_toggle)</strong> length: ' + expLen + '</p>';
      html += '<p class="debug-sample"><strong>Expected (µs):</strong> ' + expShow.join(', ') + (expPulses.length > 40 ? ' …' : '') + '</p>';
      html += '<p class="debug-hint">Compare the two rows: similar values (~380 and ~770, or ~430 and ~940) mean the remote matches our protocol. If the remote uses different timing, we can adjust short/long in firmware to match.</p>';
      setResult('compare-remote-result', html);
    }).catch(function () {
      setResult('compare-remote-result', 'Request failed. Ensure the receiver is connected and try again.', true);
    });
  });

  document.getElementById('btn-last-rf') && document.getElementById('btn-last-rf').addEventListener('click', function () {
    setResult('last-rf-result', 'Loading…');
    fetch('/last-rf')
      .then(function (r) { return r.json(); })
      .then(function (d) {
        if (!d) { setResult('last-rf-result', 'No response', true); return; }
        var len = d.length || 0;
        var pulses = d.pulses || [];
        var html = '<p>Length: ' + len + ' pulses</p>';
        if (pulses.length) html += '<p class="debug-sample">Sample: ' + pulses.slice(0, 15).join(', ') + (pulses.length > 15 ? ' …' : '') + '</p>';
        setResult('last-rf-result', len ? html : '<p>No capture yet. Use the remote or run Verify TX.</p>');
      })
      .catch(function () { setResult('last-rf-result', 'Request failed', true); });
  });

  document.getElementById('btn-debug-pulses') && document.getElementById('btn-debug-pulses').addEventListener('click', function () {
    var cmd = (document.getElementById('debug-pulses-cmd') && document.getElementById('debug-pulses-cmd').value) || 'light_toggle';
    setResult('debug-pulses-result', 'Loading…');
    fetch('/debug-pulses?cmd=' + encodeURIComponent(cmd))
      .then(function (r) { return r.json(); })
      .then(function (d) {
        if (!d) { setResult('debug-pulses-result', 'No response', true); return; }
        var len = d.length || 0;
        var pulses = d.pulses || [];
        var html = '<p>Command: ' + (d.cmd || cmd) + ', length: ' + len + ' (no transmit)</p>';
        if (pulses.length) html += '<p class="debug-sample">Sample: ' + pulses.slice(0, 15).join(', ') + (pulses.length > 15 ? ' …' : '') + '</p>';
        setResult('debug-pulses-result', html);
      })
      .catch(function () { setResult('debug-pulses-result', 'Request failed', true); });
  });

  document.getElementById('btn-debug-gpio') && document.getElementById('btn-debug-gpio').addEventListener('click', function () {
    setResult('debug-gpio-result', 'Loading…');
    fetch('/debug-gpio')
      .then(function (r) { return r.json(); })
      .then(function (d) {
        if (!d) { setResult('debug-gpio-result', 'No response', true); return; }
        var html = '<p>TX pin: ' + (d.tx_pin ?? '') + ', TX invert: ' + (d.tx_invert ?? '') + ', RX pin: ' + (d.rx_pin ?? '') + '</p>' +
          '<p>Short µs: ' + (d.short_us ?? '') + ', Long µs: ' + (d.long_us ?? '') + ', Gap ms: ' + (d.gap_ms ?? '') + ', Repeats: ' + (d.repeats ?? '') + '</p>';
        setResult('debug-gpio-result', html);
      })
      .catch(function () { setResult('debug-gpio-result', 'Request failed', true); });
  });
})();
