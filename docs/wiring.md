# Harbor Breeze Fan — 315 MHz Transmitter Wiring

This project uses a **315 MHz transmitter** (e.g. FS1000A) to send codes to a Harbor Breeze ceiling fan remote (FCC ID A25-TX012). Only the transmitter is required; there is no receiver.

## Small breadboard (17 rows × 2×5) — recommended to avoid GND/5V shorts

If you see hot connections (shorts) between GND and 5 V, the breadboard or crowded wiring is a likely cause. Using a **smaller, known-good breadboard** and a strict layout helps.

### Board layout

- **Breadboard:** 17 rows, two terminal strips of 5 holes each per row (left and right of the center gap).
- **ESP32:** Sits across the gap. **Left-side pins** in **column 5** (left strip); **right-side pins** in **column 4** (right strip). The board uses the first 8 rows (pins in rows 1–8). The module body blocks 4 holes on the right strip in rows 1–9 (column 5 on the right).
- **5 V (board only):** Use the **lower-left** area for the **ESP32’s** 5 V if needed (e.g. for other peripherals). **Do not** connect the transmitter VCC to the board’s 5 V — power the transmitter from an external 5 V supply only (see Connections and troubleshooting).
- **GND:** Use a **dedicated GND row** (e.g. row 16 or 17). Run ESP32 GND here. Run the **transmitter GND** and the **external 5 V supply’s GND** to the same row (common GND). **Keep GND and 5 V on different rows** to avoid shorts.

### Suggested hole assignment (by row, left strip = L1–L5, right strip = R1–R5)

| Row | Use |
|-----|-----|
| 1–8 | ESP32 pins (left pins in L5, right pins in R4; R5 blocked by board) |
| 9   | Optional: 330 Ω resistor — one leg in L2, other leg in L3 (or use a free row) |
| 10  | **GPIO 6** → resistor (e.g. L2); resistor → wire to transmitter DATA (e.g. L3) |
| 11  | Leave free or use for resistor if 9/10 are tight |
| 12  | Leave free |
| 13  | Leave free |
| 14  | **Do not** put transmitter VCC here. (Optional: board 5 V for other uses only.) |
| 15  | Leave free or use for external 5 V distribution **for the transmitter** (transmitter VCC → external adapter 5 V only). |
| 16  | **GND (common):** ESP32 GND + transmitter GND + external 5 V supply GND |
| 17  | **GND** (extra tap) — jumper from row 16 |

- **Resistor:** 330 Ω between **GPIO 6** and **transmitter DATA** only. No 5 V or GND through the resistor.
- **Transmitter:** VCC → **external 5 V only** (e.g. USB wall adapter; not the board’s 5 V pin). GND → common GND (row 16/17). DATA → resistor (row 10 or 9).
- **Receiver (optional):** VCC → 3.3 V or 5 V (per module), GND → same GND row, DATA → GPIO 5. If the board or USB misbehaves with the receiver powered from the board, use an external supply for the receiver with common GND.

### Avoiding GND/5V shorts

1. **One row for 5 V, one for GND** — Do not put 5 V and GND in the same row or in adjacent rows if the strip is shared.
2. **Short jumpers** — Use the shortest jumpers that reach; long or loose wires can touch and short.
3. **Inspect the breadboard** — On the small board, verify with a multimeter (continuity) that no 5 V hole is shorted to a GND hole with nothing plugged in. If you see continuity, the board is bad.
4. **Power last** — Wire GND and signal (GPIO 6 → resistor → DATA) first; add 5 V last and double-check before applying power.
5. **Rail orientation** — If the small board has +/- rails along the edge, use the **left (or right) rail for 5 V only** and the **other rail for GND only**; do not mix 5 V and GND on the same rail.

## Connections

| Transmitter pin | Connection |
|-----------------|------------|
| GND             | Common GND with ESP32 (e.g. breadboard GND row). **Do not** power the transmitter from the board — see below. |
| VCC             | **External 5 V only** (USB wall adapter, power bank, or second USB port). **Never** connect transmitter VCC to the ESP32 board’s 5 V or 3V3 pins — doing so can take the device offline (no USB, no WiFi). |
| DATA            | GPIO 6 → **330 Ω resistor** → transmitter DATA |

| Receiver pin (optional) | Connection |
|------------------------|------------|
| GND                    | GND (common with ESP32; required for DATA to work) |
| VCC                    | 3.3 V or 5 V (per module). If the board or USB acts up, power the receiver from an external supply with common GND. |
| DATA                   | GPIO 5 (for capturing the original remote; GET /last-rf returns the pulse list) |

- **GPIO 6** is the TX output. The firmware toggles it for the encoded signal. If the fan does not react, the transmitter may be active-low; set `TX_INVERT` to `1` in `src/main.cpp` (idle and pulse levels are then inverted).
- **330 Ω series resistor** between GPIO 6 and the transmitter DATA pin is recommended to limit current and reduce spurious RF.
- Power the transmitter from a **separate 5 V supply** (not the board’s 5 V or 3V3). Use **common GND** between the board and that supply. Connecting the transmitter’s VCC to the board’s 5 V or 3V3 can make the whole device unreachable (no USB, no WiFi).

## GPIO 5 and GPIO 6 — why these pins (ESP32-C3-DevKitM-1)

For **ESP32-C3-DevKitM-1** (and ESP32-C3-MINI-1), GPIO 5 and GPIO 6 are appropriate and among the best choices:

| Pin   | Use in this project | ESP32-C3 note |
|-------|---------------------|----------------|
| **GPIO 6** | TX (transmitter DATA out) | General-purpose GPIO; no strapping, no onboard function. **Ideal for TX.** |
| **GPIO 5** | RX (receiver DATA in, optional) | General-purpose GPIO; not a strapping pin. ADC2 is on GPIO 5 — that only matters for *analog* reads (ADC2 is unusable during Wi‑Fi). For **digital input** (RF capture), GPIO 5 is fine. |

**Pins to avoid for external wiring:**

- **GPIO 2, 8, 9** — Strapping pins; level at reset affects boot/flash. GPIO 8 also drives the onboard RGB LED.
- **GPIO 18, 19** — USB D‑/D+ on this board; do not use for peripherals.
- **GPIO 9** — Boot button; external circuits that pull it low at reset can trigger download mode.

**Alternatives if you ever need to move pins:** GPIO 4 and GPIO 7 are also safe, non-strapping pins. You could use GPIO 4 for RX and GPIO 7 for TX; change `TX_PIN` in `src/main.cpp` and `RF_RECV_PIN` in `src/rf_capture.cpp` (and any debug strings that mention the pin numbers).

## Parts

| Part | Role |
|------|------|
| ESP32-C3 (or compatible) | Board running the firmware |
| 315 MHz transmitter (e.g. FS1000A) | RF send; DATA driven from GPIO 6. Use good jumper wires—the transmitter is sensitive to bad or loose connections (see "Transmitter quality and wiring" below). |
| 330 Ω resistor | Series: GPIO 6 → resistor → transmitter DATA |
| 330–470 Ω resistor + LED (optional) | 5 V indicator: 5 V → resistor → LED → GND (see “Optional: 5 V indicator LED” below) |
| Jumper wires | 5 V, GND, signal — use solid connections; bad wires can make the transmitter fail. |

## Transmitter quality and wiring

- **Shorts out of the box:** A significant share of cheap 315 MHz transmitter modules are **shorted** (VCC to GND) when new. Before wiring, test with a multimeter in continuity mode: with the module **unplugged**, there should be **no beep** between VCC and GND. If it beeps, the module is bad—do not use it. Ordering a spare or two is recommended.
- **Sensitive to bad wires:** The transmitter often fails or acts flaky with a poor GND or power connection (loose breadboard hole, broken strand, thin or long wire). If the transmitter doesn't work, try a **different GND wire** and a **different GND hole** on the common GND row before changing the circuit. Use short, solid jumpers for VCC and GND.

## Antenna

Many 315 MHz modules have a solder pad or hole for an antenna. Adding a wire antenna (~25 cm) can improve range.

## Optional: 5 V indicator LED

To confirm that your external 5 V supply is on, add an LED and a **current-limiting resistor** in series between 5 V and GND. Use the same 5 V and GND as the transmitter (e.g. from the USB adapter). **Do not** connect an LED directly to 5 V without a resistor or it can burn out.

- **Wiring:** 5 V → **resistor (330–470 Ω)** → LED **anode (long leg)** → LED **cathode (short leg)** → GND.
- **Resistor:** 330 Ω or 470 Ω is a good choice for a standard red/green/yellow LED at 5 V (~10–15 mA). 220 Ω is OK (brighter); 1 kΩ is OK (dimmer).
- When 5 V is present, the LED lights; when the supply is off or disconnected, the LED is off.

**USB-C and 5 V:** Many USB-C ports only supply 5 V when they detect a device on the **CC (Configuration Channel)** pin—usually via a **5.1 kΩ resistor from CC to GND** in the cable or adapter. That is not “data” (D+/D‑); it is power negotiation. If you use a **USB-C** port and get no 5 V, try a **USB-A** charger or cable instead (USB-A often supplies 5 V as soon as something is plugged in), or use a USB-C adapter/cable that supports charge-only (with correct CC termination).

**Wiring the 5.1 kΩ resistor (USB-C sink):** One leg of the resistor → **CC** (Configuration Channel pin or wire on the USB-C plug or breakout). Other leg → **GND** (same GND as your 5 V return). Your **VBUS** and **GND** from the cable still go to your 5 V and GND rails (transmitter, LED). The 5.1 kΩ is only between **CC** and **GND**; it does not go in series with power. If the connector has two CC pins (CC1, CC2), use one 5.1 kΩ from CC1 to GND and one from CC2 to GND so either cable orientation works; or a single resistor on the one CC line your cable exposes.

**Which wire is CC?** USB-C cable colors are not standardized. Yellow is sometimes CC; green and white are often D+ and D‑. To find CC: with the cable plugged in and GND connected, try the 5.1 kΩ from **each** of the non‑red, non‑black wires to GND in turn; when 5 V appears on the red (VBUS) wire, that wire is almost certainly CC. Or use a multimeter in continuity mode between the USB-C plug’s CC pin (see pinout) and each wire.

**If the 5.1 kΩ on yellow / green / white doesn’t give 5 V:** Many USB-C cables don’t bring CC out to the wire end (CC is only inside the plug), or the port wants different handshaking. The most reliable way to get 5 V for the transmitter and LED is to use a **USB-A** source: a **USB-A wall charger** or **USB-A port** with a **USB-A to USB-C** (or USB-A to whatever you need) cable. USB-A usually supplies 5 V as soon as something is plugged in, with no CC resistor. Use the charger’s 5 V (red) and GND (black) for your circuit.

**If the LED does not turn on:**

1. **Polarity:** LEDs only work one way. Try **reversing the LED** (swap the two legs). The **anode (long leg)** must be toward 5 V (via the resistor); **cathode (short leg)** toward GND. If your LED has no long leg, the flat side of the rim is usually the cathode.
2. **5 V really on?** Confirm the adapter is plugged in and the same 5 V / GND rails power the transmitter. If the transmitter works when you send a command, 5 V is present — then the problem is only the LED circuit (polarity, loose wire, or bad LED).
3. **Connections:** Check that the resistor and LED are in one path: 5 V → resistor → LED → GND with no loose jumps or wrong breadboard rows. A single break or wrong row will leave the LED off.
4. **Try another LED or resistor:** Swap in a different LED (or temporarily try without the resistor for a **very short** test only — 5 V can kill an LED if left unregulated; one quick touch is sometimes used to verify polarity). Prefer flipping the LED first.

## Single outlet, minimal footprint

To run everything from **one wall plug** with a small setup:

**What you need:** One 5 V USB adapter (2 A is plenty for one transmitter), **common GND** (ESP32 GND and transmitter GND on the same row—use a good wire; see troubleshooting if the transmitter is flaky), and the **transmitter 5 V from the adapter** (not from the ESP32’s 5 V pin).

### Simplest: one cable + breakout

1. **One USB cable** from the adapter to a **USB breakout** (exposes 5 V and GND).
2. **Common GND:** One breadboard row for GND. Run breakout GND, ESP32 GND, and transmitter GND to that row (short, solid jumpers).
3. **5 V:** From the breakout, run 5 V to the **ESP32** (e.g. power it via USB from a short cable off the breakout, or use the board’s 5 V/VIN if it has one). Run a **separate 5 V wire** from the breakout to the **transmitter VCC**. Do not connect transmitter VCC to the ESP32.
4. **Signal:** GPIO 6 → 330 Ω → transmitter DATA.

One plug, one cable, one breakout, two 5 V wires and one GND row.

### Optional: bulk cap or filter

If the ESP32 resets or USB drops when the transmitter keys, add a **100–470 µF** electrolytic across 5 V and GND at the breakout, and/or a **ferrite bead + 10–22 µF** on the transmitter VCC wire only. Often a good GND connection (and a decent 2 A adapter) is sufficient without these.

### Minimal parts

- 5 V 2 A USB wall adapter, one USB cable, one USB breakout.
- Breadboard, ESP32, transmitter, 330 Ω, jumpers. Optional: ~25 cm wire on the transmitter antenna pad for range.

## Build

- Set WiFi credentials (see README), then build and upload firmware and filesystem:

```bash
pio run -t upload
pio run -t buildfs
pio run -t uploadfs
```

Open `http://<device-ip>/` in a browser to use the fan control UI.

## Troubleshooting: transmitter on but fan does not respond

1. **Verify the transmitter is sending (receiver on GPIO 5):** With the receiver powered (external supply, common GND), use the **Debug & settings** panel on the homepage: pick a command and click **Verify TX**. The device sends the command and reports whether the receiver captured it. **TX seen by receiver: Yes** means the TX is working (check range, DIP, or fan). **No** means the receiver didn’t see the transmission (check TX wiring, power, antenna, or try flipping **TX invert** in the same panel). **Interpreting results:** Expected pulses are ~380 µs and ~770 µs (match harborBreeze315). If **Captured sample** shows values in the **thousands** (e.g. 5000–11000 µs), the receiver is likely seeing noise or a different source, not our TX — confirm transmitter and receiver wiring, antenna, and that the transmitter has 5 V (e.g. from USB-A). **New capture during test: Yes** means a new burst was stored during the test; if it’s still the wrong length/timing, the receiver isn’t decoding our transmission correctly. **TX seen: Yes but captured sample values don’t match 430/940 µs:** The receiver is picking up the transmission but pulse timing is distorted (e.g. 495, 148, 879, 3000 instead of 430, 940). Try the fan anyway (it may still respond); try flipping **TX invert**; or move the receiver slightly away from the transmitter to avoid saturation.
2. **Try active-low (TX invert):** Many 315 MHz modules transmit when DATA is **LOW**. On the homepage, open **Debug & settings** and set **TX invert** to **1 (active-low)** or **0 (active-high)**; the setting is saved on the device. If the fan doesn’t react, flip it and try again, then re-run **Verify TX**.
3. **Range and antenna:** Keep the transmitter within a few meters of the fan; add a ~25 cm wire antenna to the module if it has a pad or hole.
4. **DIP switch:** The firmware uses codes for **DIP 1**. If your fan/remote is set to another DIP position, the fan won’t respond; set the physical DIP on the fan receiver to 1 to match.
5. **Compare with physical remote (light):** Use the **Debug & settings** panel: point the fan’s remote at the receiver, press **Light On/Off** once, then click **Capture remote & compare (light_toggle)**. The page shows the remote’s captured pulse list next to our expected light_toggle. If the remote’s values are consistently different (e.g. ~430/940 instead of ~380/770), we can change the firmware’s short/long timing to match the remote.

## Troubleshooting: hot connections (GND ↔ 5 V)

If the board or wires get hot, or the ESP32 resets when the transmitter is connected, you likely have a short between GND and 5 V. Use the **small breadboard** layout above: one row for 5 V only, one for GND only, and verify with a multimeter that 5 V and GND are not shorted before powering on.

## Troubleshooting: board stops when receiver is connected (GND + 5 V)

If the ESP32 stops working (resets, no boot, or disconnects) as soon as you plug in the receiver’s GND and 5 V, the cause is usually one of the following.

### 1. Power the receiver from 3.3 V instead of 5 V (recommended)

The board’s 5 V comes from USB; the transmitter already uses that rail. Adding the receiver to 5 V can overload the USB supply or cause a brownout. Many 315 MHz receiver modules work fine at **3.3 V**.

- **Change the receiver VCC** from the 5 V pin to the **3V3** pin on the ESP32 (same GND).
- Keep the transmitter on 5 V as before.
- If the board then stays up and the receiver still picks up the remote, you can leave it on 3.3 V.

### 2. Check receiver pinout

Confirm the module’s pins: **VCC**, **GND**, **DATA** (sometimes labeled **OUT** or **D**). If VCC and GND are swapped, connecting power will short 5 V to GND and the board will shut down or reset. Check the module’s datasheet or silkscreen before wiring.

### 3. Rule out a bad receiver or short

- With the receiver **unplugged**, measure with a multimeter: between the receiver’s VCC and GND pins, there should be **no continuity** (no beep). If there is, the module is shorted — do not use it on the board’s 5 V.
- Try another 315 MHz receiver if you have one; if the board only fails with a specific module, that module may be faulty.

### 4. If the receiver must have 5 V

If your module only works at 5 V and 3.3 V didn’t help, power it from a **separate** 5 V supply (e.g. another USB cable or 5 V adapter). Use a **common GND** between the ESP32 and the external supply (connect ESP32 GND to the external supply’s GND). Do not connect the external 5 V to the ESP32’s 5 V pin — only to the receiver’s VCC.

## Troubleshooting: Transceiver doesn’t work with one GND wire but works with another

If the transceiver fails when its GND is connected one way (e.g. a wire straight to the ESP32 GND) but **works when you change only that GND wire or where it plugs in** (same supply, same topology), the cause is likely the **connection**, not the circuit design.

**Possible causes:**

- **Bad or marginal wire:** The first wire may be faulty (high resistance, broken strand, corrosion, or a bad crimp). Try a **different GND wire** between the transceiver and the common GND row.
- **Poor breadboard contact:** The hole or row you used first may have a loose or oxidized contact. Move the transceiver’s GND to a **different hole on the same GND row**, or to another GND row that’s jumpered to the same common GND.
- **Thin or long wire:** A very thin or long wire can have enough resistance that when the transmitter draws current, the voltage drop makes the link unreliable. Use a **short, sturdy jumper** for the transceiver GND.

So: **try a different GND wire and/or a different GND hole** before assuming you need a different power setup. Often that’s enough.

## Troubleshooting: Receiver not working after tying ground to ESP32

Tying the receiver’s GND to the ESP32’s GND (common ground) is correct. If the receiver then stops working, check the following:

1. **Receiver still has power (VCC):** The receiver needs **both** VCC and GND. Moving only the GND wire is correct; make sure **VCC** is still connected. If the receiver is powered from an **external supply**, connect that supply’s **GND** to the **same** breadboard row as the receiver GND and ESP32 GND (one common node). The external supply’s **positive** stays on the receiver’s VCC only.
2. **DATA still on GPIO 5:** Confirm the receiver’s **DATA** (or OUT) pin is still firmly connected to **GPIO 5** on the ESP32. If the DATA wire was pulled or moved when you changed the ground, re-seat it.
3. **No loose wires:** After changing one wire, re-check all three: receiver **VCC** → supply (3.3 V / 5 V), receiver **GND** → common GND row (with ESP32 GND), receiver **DATA** → GPIO 5.
4. **If you switched receiver power to the board:** If you moved the receiver’s VCC from an external supply to the ESP32’s 3V3 or 5 V when you tied grounds, the board or USB may misbehave (see “board stops when receiver” and “USB not visible”). Prefer powering the receiver from an **external** supply with **common GND** so the receiver has a stable supply and the ESP32 stays stable.

## Troubleshooting: USB not visible when transmitter is connected (5 V and GND)

If the board stays on with the transmitter powered from the board’s 5 V and GND, but the **USB serial port disappears** (computer no longer sees the device for programming or serial monitor), the transmitter is overloading the **5 V rail** that comes from USB. The ESP32 may still run (e.g. over WiFi), but the USB‑to‑UART bridge or the 5 V supply sags and USB enumeration fails.

**Fix: power the transmitter from an external 5 V supply**

- Do **not** power the transmitter from the ESP32 board’s 5 V pin.
- Use a **separate** 5 V source for the transmitter: a second USB cable, a 5 V wall adapter, or a USB power bank.
- Connect **common GND**: tie the external supply’s GND to the ESP32 GND (e.g. on the breadboard). The DATA signal (GPIO 6 → resistor → transmitter DATA) is referenced to this common ground.
- Connect the transmitter’s **VCC** only to the external 5 V; connect the transmitter’s **GND** to the common GND. Leave the board’s 5 V pin unconnected to the transmitter.

With the transmitter powered externally, the board’s USB 5 V is used only for the ESP32 and the onboard regulator/bridge, so the USB port should stay visible while the transmitter is connected.

## Troubleshooting: Device unreachable (no USB, no WiFi) when transmitter is on board 5 V or 3V3

If connecting the transmitter’s **VCC and GND** to the board’s **5 V and GND** (or 3V3 and GND) makes the **entire device go offline** — no serial port, no WiFi, no web UI — the board’s power rail is being shorted or overloaded. This can happen even with a “5 V USB plugged in directly” to the board if the transmitter is still powered from the board.

### Do not power the transmitter from the board

- **Do not** connect the transmitter’s **VCC** to the ESP32 board’s **5 V** or **3V3** pins. Any connection from transmitter VCC to the board will draw current from (or short) that rail and can take the device down.
- **Do** power the transmitter from a **completely separate 5 V supply** (e.g. a USB wall adapter or power bank). Use **only**:
  - Transmitter **VCC** → 5 V from the **external** supply (nothing from the ESP32 board).
  - Transmitter **GND** → **common GND** (same GND as the ESP32, e.g. one breadboard GND row with a jumper from board GND to the external supply’s GND).
  - Transmitter **DATA** ← GPIO 6 via 330 Ω (unchanged).
- The ESP32 board is powered only by its own USB cable. The transmitter is powered only by the external 5 V. They share **GND** so the DATA signal is valid.

### If it still fails with external 5 V

- **Test the transmitter for a short:** With the transmitter **unplugged** from everything, use a multimeter in continuity (beep) mode. Between the transmitter’s **VCC** and **GND** pins there should be **no beep** (open circuit). If it beeps, the module is shorted — replace it; do not connect it to the board’s rails.
- **Double-check wiring:** Ensure no jumper or breadboard hole connects the **board’s 5 V or 3V3** to the transmitter. The only link between board and transmitter should be **GND** (common) and **DATA** (GPIO 6 → resistor → transmitter DATA).
