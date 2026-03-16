# Harbor Breeze Fan Control

ESP32 firmware and web UI to control a **Harbor Breeze ceiling fan** (315 MHz remote, FCC ID [A25-TX012](https://fccid.io/A25-TX012)) using a 315 MHz transmitter (e.g. FS1000A). No physical remote required once configured.

Based on the protocol and codes from [harborBreeze315](https://github.com/bttnns/harborBreeze315) (Raspberry Pi + Python); this project ports the encoding to C++ and runs on an ESP32 with a simple web interface.

## Hardware

- **ESP32-C3** (or compatible) dev board
- **315 MHz transmitter** (e.g. FS1000A): VCC → 5 V, GND → GND, DATA ← GPIO 6 (via 330 Ω resistor)

See [docs/wiring.md](docs/wiring.md) for full wiring. For **one plug and a small footprint**, see the “Single outlet, minimal footprint” section there. 
## Build and upload

1. **WiFi:** Copy `.env.example` to `.env` and set your network credentials:

   ```bash
   cp .env.example .env
   # Edit .env and set WIFI_SSID and WIFI_PASS
   ```

   `.env` is gitignored. The build loads it automatically and injects the values into the firmware.

2. **Firmware and filesystem:**

   ```bash
   pio run -t upload
   pio run -t buildfs
   pio run -t uploadfs
   ```

   **Transceiver-only (production):** To build without the optional receiver (no GPIO 5, no learn-from-remote or Verify TX):

   ```bash
   pio run -e esp32c3_tx -t upload
   pio run -t buildfs
   pio run -t uploadfs
   ```

   The web UI will hide receiver-only features (learn Home Shield from remote, Verify TX, Last RF, etc.) and show only transceiver controls. Home Shield can still be used if you restore it from backup in Debug.

3. Get the device IP: open the serial monitor (`pio device monitor`). On ESP32-C3, the board uses `printf()` for logging—you’ll see `[HB] IP: x.x.x.x` at boot and then every second. If you open the monitor after boot, wait 5–10 seconds for the heartbeat. Then visit **http://&lt;device-ip&gt;/** in a browser. You can also fetch **GET /ip** (e.g. from another device on the same network) to get the IP as plain text.

**If the page shows “app.js did not load” or the console shows WebSocket `/ws` or 404s for `/saved`, `/saved-rf`, `/last-rf`:** the device is serving a different project’s filesystem. From this repo run `pio run -t buildfs` then `pio run -t uploadfs`, then hard-refresh the browser (Ctrl+Shift+R / Cmd+Shift+R). You should then see “Ready — tap a button to send” and the Harbor Breeze buttons only.

## Using on Android

1. **Same WiFi** — Connect your phone to the same network as the ESP32.
2. **Get the device IP** — From the serial monitor you'll see `[HB] IP: x.x.x.x`. Or from any device on the network (e.g. a computer), open `http://<device-ip>/ip` in a browser to see the IP as plain text.
3. **Open the UI** — On your phone, open Chrome (or another browser) and go to **http://&lt;device-ip&gt;/** (replace with the actual IP, e.g. `http://192.168.1.42/`).
4. **Add to Home screen (optional)** — In Chrome: menu (⋮) → **Add to Home screen** or **Install app**. The fan control will open like an app, with a dark theme and no browser chrome. You can then open it from your home screen with one tap.

The web UI is a PWA: it uses a manifest and mobile meta tags so Android treats it like an app when added to the home screen.

## Web UI

The single-page interface provides:

- **Light:** On/Off (toggle), Dim
- **Fan:** Off, Speed 1–6, Direction (Summer / Winter), Nature Breeze
- **Delay off:** Off, 2 h, 4 h, 8 h
- **Extras:** Home Shield
- **Debug & settings:** TX invert (active-low) toggle (saved to device; try flipping if the fan doesn’t react), **Verify TX** (send a command and see if the receiver captured it), **Last RF** capture, **Expected pulses** (no transmit), and **GPIO state**

Each button sends the corresponding Harbor Breeze code over 315 MHz (6 repeats with 8 ms gap, as in the original remote).

## API

- **GET /ip** — Plain text response with the device IP (e.g. for scripts or when you can’t see the serial monitor).
- **POST /send** — Body: `{ "cmd": "light_toggle" }` (or any command name). Returns `{ "ok": true, "cmd": "..." }` or an error.
- **GET /send?cmd=light_toggle** — Same via query parameter.
- **GET /commands** — Returns `{ "commands": [ "light_toggle", ... ] }`.
- **GET /debug-pulses?cmd=light_toggle** — Returns `{ "cmd", "length", "pulses": [ ... ] }` for the encoded pulse array **without transmitting**. Use to compare with a receiver capture.
- **GET /verify-tx?cmd=light_toggle** — **Sends** the command (transmits), waits for the onboard receiver to capture, then returns `{ "expected_length", "captured_length", "tx_seen_by_receiver", "expected_sample", "captured_sample" }`. Use to confirm the transmitter is putting out RF when the fan doesn’t respond (receiver on GPIO 5, see [docs/wiring.md](docs/wiring.md)).

Command names: `light_toggle`, `light_dim`, `fan_off`, `fan_speed_1` … `fan_speed_6`, `fan_direction_summer`, `fan_direction_winter`, `nature_breeze`, `delay_off`, `delay_2h`, `delay_4h`, `delay_8h`, `home_shield`.

**Home Shield** is learned from your remote: press the remote’s Home Shield button, then **Refresh last RF** (Debug), then **Use last capture as Home Shield**. Send via **GET /send-hub?cmd=home_shield**. Optional: **&raw=1** sends the full last capture repeated 12× (no learn step); **&gap=1** adds 8 ms between repeats. **Not all Harbor Breeze models support Home Shield** — if the physical remote’s Home Shield button does nothing (no double-blink, no away mode), the fan likely doesn’t support it.

**Persisting Home Shield across flashes:** The learned frame is stored in NVS and usually survives a normal **firmware** upload (`pio run -t upload`). If you do a full erase or reflash filesystem, it can be lost. To back up: **GET /learned-home-shield** and save the JSON (e.g. `home-shield-backup.json`). To restore after a flash: **POST /restore-home-shield** with that JSON as body (e.g. `http --print=b POST http://<device-ip>/restore-home-shield < home-shield-backup.json`).

**Get the 10 hub symbols without flashing:** If you already have Home Shield learned on the device, you can read the pulse array and decode it on your machine: **GET /learned-home-shield** (e.g. `curl -s http://<device-ip>/learned-home-shield > frame.json`), then run **`python3 scripts/decode_learned_home_shield.py frame.json`**. The script prints the 10 command symbols and a C array line you can paste into `HUB_HOME_SHIELD` in `src/harbor_breeze.cpp`.

## Verify transmitter with the receiver (fan not responding)

If the fan doesn’t react but the UI seems to send: with the **receiver** on GPIO 5 (powered externally, common GND), open **`http://<device-ip>/verify-tx?cmd=light_toggle`**. The device transmits, then reports whether the receiver saw the burst. **`tx_seen_by_receiver: true`** means the TX is working (check range, antenna, DIP, fan). **`tx_seen_by_receiver: false`** means the receiver didn’t see the transmission (check TX wiring, power, or try changing `TX_INVERT` in `src/main.cpp` and reflashing). See [docs/wiring.md](docs/wiring.md) for details.

## Verify codes with the original remote

To check that our encoding matches the physical remote, capture what the remote sends and compare to what we encode. You can use **this board** if it has a 315 MHz receiver on GPIO 5.

1. **Wire the receiver** (if not already): receiver DATA → GPIO 5, VCC and GND per module. See [docs/wiring.md](docs/wiring.md).

2. **Capture the remote**: flash this firmware, then point the **original Harbor Breeze remote** at the receiver and press one button (e.g. Light On/Off). Fetch `http://<device-ip>/last-rf` (or open in a browser). You get JSON: `{ "seq", "length", "pulses": [ ... ] }` — alternating pulse durations in µs. Serial will show `[HB] RF captured N pulses`.

3. **Get our encoding**: fetch `http://<device-ip>/debug-pulses?cmd=light_toggle` (same logical command). Response has the same shape: `{ "cmd", "length", "pulses": [ ... ] }`.

4. **Compare**: same `length` (~305), similar values — short ~380 µs, long ~770 µs, gaps ~8000 µs. Small differences are normal (receiver jitter). If the remote’s pulses are completely different, the protocol or DIP setting may differ; see [harborBreeze315](https://github.com/bttnns/harborBreeze315) and the FCC manual for your remote.

## Protocol

- OOK at 315 MHz; bit timing 380 µs (short) and 770 µs (long). Frame = 17-bit preamble (DIP 1) + 8-bit function; repeated 6 times with 8 ms gap. Remote DIP switch assumed **1** (see harborBreeze315 and [FCC user manual](https://fccid.io/A25-TX012/User-Manual/User-manual-1937614)).

## License

MIT. Harbor Breeze codes and protocol reference: [bttnns/harborBreeze315](https://github.com/bttnns/harborBreeze315).
