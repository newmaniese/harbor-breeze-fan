# Harbor Breeze Hub vs This Project — Protocol Comparison

If the fan is **not seeing your signals**, the [harbor-breeze-hub](https://github.com/enlilodisho/harbor-breeze-hub) (Raspberry Pi + 315 MHz TX) uses a **different protocol** that you can try. This doc summarizes the differences and what to try.

## Two Different Protocol Interpretations

| Aspect | This project (FCC A25-TX012) | harbor-breeze-hub (reverse‑engineered) |
|--------|------------------------------|----------------------------------------|
| **Source** | FCC manual / binary interpretation | RTL-SDR capture of physical remotes |
| **Encoding** | 25 bits: 17-bit preamble + 8-bit function. Bit 1 = long HIGH + short LOW; 0 = short HIGH + long LOW | Symbol-based: SS, SL, LL, LS, SR (each = ON µs + OFF µs) |
| **Timings (µs)** | Short 380, Long 770 (symmetric per bit) | SHORT_ON 400, SHORT_OFF 500, LONG_ON 850, LONG_OFF 950, REST 10000 |
| **Frame** | 1× leading LOW (770 µs) + 25 bits × 2 = 51 pulses; then **6 repeats** with **8 ms gap** | Remote ID (15 symbol pairs) + command (10 pairs) = 50 pulses; then **12 repeats** with **no gap** |
| **First edge** | First duration is **LOW** (770 µs) | First duration is **HIGH** (400 µs for remote "0") |

So:

- **Timings**: Hub uses **400 / 500 / 850 / 950** µs, not 380 / 770. Even small timing errors can prevent the fan from locking on.
- **Repetition**: Hub sends **12** full frames back-to-back with **no gap**. We send **6** with **8 ms** gap. More repeats and no gap may help.
- **Structure**: Hub uses a **symbol preamble** (e.g. remote "0" = 15× SL) plus a **symbol command**, not a 17+8 bit string. If your remote matches the hub’s remotes (DIP 0 → remote id "0"), the fan may only accept that format.

## What to Try (in order)

1. **Capture your physical remote**  
   Use **Debug & settings → Capture remote & compare (light_toggle)**. Point the fan’s remote at the receiver and press Light On/Off once.  
   - If captured pulses are **~400, 500, 850, 950** → your remote matches the **hub** protocol; use the **hub-style sender** (e.g. `/send-hub?cmd=light_toggle`).  
   - If captured pulses are **~380, 770** (or similar symmetric short/long) → keep the current binary protocol but you can still try more repeats and hub timings.

2. **Try hub-style transmission**  
   Use the **hub protocol** option in the firmware (see below): same message as “light toggle” but with hub timings, remote id "0", and **12 repeats, no gap**. If the fan responds, your hardware follows the hub’s reverse‑engineered format.

3. **Try more repeats and no gap (current encoding)**  
   In code: increase `HB_REPEATS` to **12** and set `HB_GAP_MS` to **0** (no gap between repeats). Rebuild and test. This keeps your current 380/770 and 25-bit frame but matches the hub’s repetition style.

4. **Try hub timings with current encoding**  
   If you stay with the 25-bit frame but want to test hub timings: set short to **400**, long to **850** (or use 500/950 for the “off” half; you may need to adjust to 400/500 and 850/950 per half-pulse). The hub uses **asymmetric** short (400+500) and long (850+950); our current code uses symmetric 380/770 for each bit. So “try hub timings” might mean adding a separate hub encoder (done below).

5. **TX polarity (already supported)**  
   You already have **TX invert** in Debug & settings. Try **0** and **1**; many 315 MHz modules transmit when DATA is LOW.

6. **DIP switch**  
   Hub’s remote id **"0"** is for **DIP switch = 0** on the back of the remote. Set your fan’s remote to DIP 0 and use remote id "0" in the hub-style sender.

## Hub Remote ID "0" and Light Toggle (exact)

From `fanremote_config.json`:

- **Remote "0"**: 15× SL → 15 × (SHORT_ON, LONG_OFF) = 15 × (400, 950) µs.  
- **Light power**: `["SL","SL","SS","LS","LL","SS","LL","SS","LL","SR"]`  
  → (400,950), (400,950), (400,500), (850,500), (850,950), (400,500), (850,950), (400,500), (850,950), (400, 10000).

One frame = remote "0" + light power = 25 symbol pairs = 50 timings. Hub sends this **12 times** in a row (no gap). The firmware’s hub encoder does the same so you can test without a Pi.

## References

- [harbor-breeze-hub](https://github.com/enlilodisho/harbor-breeze-hub) — Raspberry Pi, Node, `onoff` GPIO.  
- Hub’s `RFTransmitter.js`: starts with **HIGH** (`nextValue = 1`), then alternates; each timing is one duration.  
- Hub’s `fanremote_config.json`: `timings` and `remote_ids`, `commands`.
