(function () {
  fetch('/config').then(function (r) { return r.ok ? r.json() : null; }).then(function (d) {
    if (d && d.transceiver_only) document.body.classList.add('transceiver-only');
  }).catch(function () {});

  function setResult(id, html, isError) {
    var el = document.getElementById(id);
    if (!el) return;
    el.innerHTML = html;
    el.className = 'debug-result' + (isError ? ' debug-result-error' : '');
  }

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
          html += '<p class="debug-hint">Captured values in ms range — receiver may not be seeing our TX (legacy protocol).</p>';
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
        setResult('compare-remote-result', 'Failed to load. Press the remote’s Light button, then try again.', true);
        return;
      }
      var capPulses = captured.pulses || [];
      var expPulses = expected.pulses || [];
      var skip = (capPulses[0] >= 60000) ? 1 : 0;
      var html = '<p><strong>Remote</strong> length: ' + (captured.length || 0) + '</p><p class="debug-sample">' + capPulses.slice(skip, skip + 20).join(', ') + ' …</p>';
      html += '<p><strong>Expected (light_toggle)</strong> length: ' + (expected.length || 0) + '</p><p class="debug-sample">' + expPulses.slice(0, 20).join(', ') + ' …</p>';
      setResult('compare-remote-result', html);
    }).catch(function () { setResult('compare-remote-result', 'Request failed', true); });
  });

  document.getElementById('btn-decode-hub') && document.getElementById('btn-decode-hub').addEventListener('click', function () {
    setResult('decode-hub-result', 'Loading…');
    fetch('/last-rf-decode-hub')
      .then(function (r) { return r.json(); })
      .then(function (d) {
        if (!d) { setResult('decode-hub-result', 'No response', true); return; }
        if (!d.ok) {
          setResult('decode-hub-result', '<p>' + (d.error || 'Decode failed') + '</p>', true);
          return;
        }
        var html = '<p><strong>Symbols (25):</strong> ' + (d.symbols || '—') + '</p>';
        if (d.matched_cmd) html += '<p><strong>Matches command:</strong> <code>' + d.matched_cmd + '</code></p>';
        else html += '<p class="debug-hint">Command not in our list. Use the 10 command symbols (last 10 above) to add a new hub code in firmware.</p>';
        setResult('decode-hub-result', html);
      })
      .catch(function () { setResult('decode-hub-result', 'Request failed', true); });
  });

  document.getElementById('btn-compare-hub') && document.getElementById('btn-compare-hub').addEventListener('click', function () {
    setResult('decode-hub-result', 'Loading…');
    Promise.all([
      fetch('/last-rf').then(function (r) { return r.json(); }),
      fetch('/debug-pulses-hub?cmd=light_toggle').then(function (r) { return r.json(); })
    ]).then(function (results) {
      var captured = results[0];
      var expected = results[1];
      if (!captured || !expected) {
        setResult('decode-hub-result', 'Failed to load. Press the remote’s Light button, then try again.', true);
        return;
      }
      var capPulses = captured.pulses || [];
      var expPulses = expected.pulses || [];
      var skip = (capPulses[0] >= 60000) ? 1 : 0;
      var cap0 = capPulses[skip], cap1 = capPulses[skip + 1];
      var near = function (a, b, t) { return a != null && b != null && Math.abs(a - b) <= (t || 200); };
      var html = '<p><strong>Your remote</strong> length: ' + (captured.length || 0) + '</p><p class="debug-sample">' + capPulses.slice(skip, skip + 20).join(', ') + ' …</p>';
      html += '<p><strong>Hub expected (light_toggle)</strong> length: ' + (expected.length || 0) + '</p><p class="debug-sample">' + expPulses.slice(0, 20).join(', ') + ' …</p>';
      if (capPulses.length >= 2) {
        if (near(cap0, 400, 200) && near(cap1, 950, 200)) html += '<p class="debug-hint">First pair looks like hub SL (~400, ~950 µs). Your remote is likely hub protocol. Try <strong>Decode last capture as Hub</strong> to confirm, then use the Controls page — it already sends hub.</p>';
        else if (near(cap0, 380, 100) && near(cap1, 770, 100)) html += '<p class="debug-hint">First pair looks like legacy (380, 770). Use legacy /send, not /send-hub.</p>';
        else if (capPulses.length < 50) html += '<p class="debug-hint">Short capture (' + capPulses.length + ' pulses). Press the remote’s Light button once, then click <strong>Refresh last RF</strong> in the section below, then run this compare again so we use the latest capture.</p>';
      }
      setResult('decode-hub-result', html);
    }).catch(function () { setResult('decode-hub-result', 'Request failed', true); });
  });

  document.getElementById('btn-last-rf') && document.getElementById('btn-last-rf').addEventListener('click', function () {
    setResult('last-rf-result', 'Loading…');
    fetch('/last-rf')
      .then(function (r) { return r.json(); })
      .then(function (d) {
        if (!d) { setResult('last-rf-result', 'No response', true); return; }
        var len = d.length || 0;
        var pulses = d.pulses || [];
        var html = len ? '<p>Length: ' + len + '</p><p class="debug-sample">' + (pulses.slice(0, 15).join(', ')) + (pulses.length > 15 ? ' …' : '') + '</p>' : '<p>No capture yet.</p>';
        setResult('last-rf-result', html);
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
        var pulses = d.pulses || [];
        var html = '<p>' + (d.cmd || cmd) + ', length: ' + (d.length || 0) + '</p><p class="debug-sample">' + pulses.slice(0, 15).join(', ') + (pulses.length > 15 ? ' …' : '') + '</p>';
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
        var html = '<p>TX pin: ' + (d.tx_pin ?? '') + ', TX invert: ' + (d.tx_invert ?? '') + ', RX: ' + (d.rx_pin ?? '') + '</p>';
        html += '<p>Short: ' + (d.short_us ?? '') + ' µs, Long: ' + (d.long_us ?? '') + ' µs</p>';
        setResult('debug-gpio-result', html);
      })
      .catch(function () { setResult('debug-gpio-result', 'Request failed', true); });
  });

  document.getElementById('btn-learn-home-shield') && document.getElementById('btn-learn-home-shield').addEventListener('click', function () {
    setResult('home-shield-result', '<p>Saving…</p>');
    fetch('/learn-home-shield', { method: 'POST' })
      .then(function (r) { return r.json(); })
      .then(function (d) {
        if (d && d.ok) setResult('home-shield-result', '<p>' + (d.message || 'Home Shield learned.') + '</p>');
        else setResult('home-shield-result', '<p>Error: ' + (d && d.error ? d.error : 'unknown') + '</p>', true);
      })
      .catch(function () { setResult('home-shield-result', '<p>Request failed</p>', true); });
  });

  document.getElementById('btn-home-shield') && document.getElementById('btn-home-shield').addEventListener('click', function () {
    setResult('home-shield-result', '<p>Sending…</p>');
    fetch('/send-hub?cmd=home_shield')
      .then(function (r) { return r.json(); })
      .then(function (d) {
        if (d && d.ok) setResult('home-shield-result', '<p>Home Shield sent.</p>');
        else setResult('home-shield-result', '<p>Error: ' + (d && d.error ? d.error : 'unknown') + '</p>', true);
      })
      .catch(function () { setResult('home-shield-result', '<p>Request failed</p>', true); });
  });
})();
