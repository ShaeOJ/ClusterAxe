```
 ▂▃▅▇█▓▒░  Z O M B I E   O S  ░▒▓█▇▅▃▂
        C L U S T E R A X E   ·   v1.6.0
     reanimated firmware for BM1370 BitAxe
   ⚛  A · S · I · C   P O O L   ISOTOPE  ⚛
```

# ClusterAxe / ZombieOS v1.6.0 — "Fallout Shelter" release

> *Transmission from the reactor bunker, cycle 1.6.0.* The web UI has been
> **fused into the core** — one atomic app image, no more `www.bin` half-life.
> Fresh telemetry pours out of a live reactor tap, the best shares get their own
> Hall of Records, and the overclock seals can now be broken from the panel
> itself. Rad-hardened, cluster-ready, and glowing. **Mine on, survivor.**

Distributed BitAxe mining firmware for ESP32‑S3. A **master** connects to the
pool and hands work to up to 8 **slaves** over ESP‑NOW (wireless) or BAP/RS‑485
(wired). This release ships prebuilt **master**, **slave**, and **standalone**
images for both the single‑chip **Gamma (board 601)** and the dual‑chip
**GammaTurbo (board 801)**.

---

## ☢ New in v1.6.0

- **Embedded web UI — the www.bin is dead.** AxeOS is now compiled straight into
  the app binary (`tools/embed_web_ui.py`). One image to flash, no separate
  website slot. A custom UI is still allowed: upload via the "Website" OTA and it
  serves from SPIFFS, self‑reverting to the embedded UI if that filesystem ever
  goes missing.
- **🔓 Unlock Overclock Mode — from the panel.** Custom frequency & voltage input
  no longer needs the hidden `?oc` URL incantation. A new **Unlock Overclock
  Mode** button on the Settings page breaks the seals, swaps the safe dropdowns
  for raw number inputs, and persists the unlock to NVS so it survives reboots.
  The **Disable Overclock Mode** button re-seals it.
- **Live telemetry reactor tap** — `/api/ws/live` streams fast‑moving readings
  (hashrate, temps, power, fan, shares, CPU, last nonce…) at ~1 Hz so the panel
  updates between the slower `/api/system/info` polls.
- **Best Shares — Hall of Records.** New scoreboard page listing the top‑20
  highest‑difficulty shares of the current uptime (`/api/system/scoreboard`,
  RAM‑only, resets on reboot).
- **Block Header shows the last found nonce** plus its difficulty, version bits,
  and nTime — updated live as the miner works.
- **Reactor diagnostics** — per‑task runtime accounting to the console plus a
  rolling `cpuUsage` field in `/api/system/info`.

*Adopted from ESP‑Miner 2.15.1; see `RELEASE_NOTES.md` for the full changelog.*

---

## Features (carried forward)

**Mining modes**
- **Standalone** — solo/pool mining on a single board, no cluster.
- **Master** — connects to the pool and distributes work to up to **8 slaves**.
- **Slave** — receives work from a master; no pool connection of its own.
- **Cluster transport:** ESP‑NOW (wireless, auto‑pairing) or BAP/RS‑485 (wired),
  with heartbeat + auto re‑registration and full‑cluster rejection handling.
- **Nonce‑space splitting** across all nodes so no two miners duplicate work.

**Pools**
- **Dual‑pool support** — primary + secondary, in failover or split mode, with a
  live primary/secondary work‑split readout.

**Tuning & protection (always‑on in every mode)**
- **Watchdog** — throttles frequency + voltage on over‑temp / under‑volt, then
  gradually auto‑recovers with hysteresis. Board‑aware thresholds (5 V Gamma vs
  12 V GammaTurbo).
- **Auto‑timing** — calibrates the ASIC job interval for best hashrate and
  persists it to NVS, so a reboot resumes instantly.
- **Benchmark tool** — voltage/frequency grid search that finds the best stable
  operating point, auto‑applies it, and saves best‑so‑far to NVS.

**Hardware**
- ASIC: **BM1370**. Boards: **Gamma 601** (1× BM1370, 5 V) and **GammaTurbo 801**
  (2× BM1370, 12 V, 36 W).

---

## Firmware manifest

| Board | Role | File | Size |
|-------|------|------|------|
| Gamma 601 (1× BM1370) | **Master** | `clusteraxe-gamma601-master.bin` | 2,240,688 B |
| Gamma 601 (1× BM1370) | **Slave**  | `clusteraxe-gamma601-slave.bin`   | 2,113,168 B |
| Gamma 601 (1× BM1370) | **Standalone** | `clusteraxe-gamma601-standalone.bin` | 2,113,712 B |
| GammaTurbo 801 (2× BM1370, 12 V) | **Master** | `clusteraxe-gt801-master.bin` | 2,240,688 B |
| GammaTurbo 801 (2× BM1370, 12 V) | **Slave**  | `clusteraxe-gt801-slave.bin`   | 2,113,168 B |
| GammaTurbo 801 (2× BM1370, 12 V) | **Standalone** | `clusteraxe-gt801-standalone.bin` | 2,113,712 B |

Each `build_*/` directory is a **complete flashable set**: `bootloader/bootloader.bin`,
`partition_table/partition-table.bin`, the app image (`zombie-os.bin`, also copied
to the `clusteraxe-*` name above), and `ota_data_initial.bin`. The web UI is now
compiled **into** the app image — there is no separate `www.bin` to flash.

> **One master per cluster.** Flash exactly one board as master; flash every
> other board as slave. Master and slave are **not** interchangeable images.

---

## ⚠️ Flashing from a stock BitAxe (AxeOS OTA) — READ THIS FIRST

The AxeOS web updater accepts files by **fixed name**. Coming over the air from a
stock BitAxe, you must **rename the app image to `esp-miner.bin`** before
uploading — AxeOS ignores any other filename.

1. Open the stock miner's web page → **Settings → Firmware Update**.
2. **Firmware slot:** take the app image for your board/role, e.g.
   `clusteraxe-gt801-master.bin`, and **rename it to `esp-miner.bin`**. Upload it.
3. Reboot. The board comes up as ZombieOS. The web UI is embedded in the app, so
   there is **nothing to upload in the "Website" slot** anymore.

OTA reflashes the app (which now carries the web UI) — not the bootloader or
partition table — exactly what you want coming from stock AxeOS.

---

## Clean flash with esptool (first‑time / recovery)

From inside the chosen `build_*/` directory:

```
python -m esptool --chip esp32s3 -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0      bootloader/bootloader.bin \
  0x8000   partition_table/partition-table.bin \
  0x10000  zombie-os.bin \
  0xf10000 ota_data_initial.bin
```

The app image is `zombie-os.bin` for **every** variant (the `clusteraxe-*` name is
a copy of it). On a Windows machine with ESP‑IDF you can also just run
`idf.py -B build_gt_master -p COMx flash`.

| Offset | Contents |
|--------|----------|
| `0x0` | bootloader |
| `0x8000` | partition table |
| `0x10000` | application (ZombieOS, includes the embedded web UI) |
| `0xf10000` | OTA data |

Target: **ESP32‑S3**, 16 MB flash, DIO @ 80 MHz. **No `www.bin` line — it's gone.**

---

## Building from source

Requires **ESP‑IDF 5.5.1** and **Node.js 22+** (the web UI builds as part of the
firmware). Each variant builds into its **own** directory so nothing overwrites:

```
build_master.bat        ->  build_master/         Gamma 601 master
build_slave.bat         ->  build_slave/          Gamma 601 slave
build_standalone.bat    ->  build_standalone/     Gamma 601 standalone (no cluster)
build_gt_master.bat     ->  build_gt_master/      GammaTurbo 801 master
build_gt_slave.bat      ->  build_gt_slave/       GammaTurbo 801 slave
build_gt_standalone.bat ->  build_gt_standalone/  GammaTurbo 801 standalone (no cluster)
```

Each script runs `idf.py -B <dir> -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.<variant>" build`,
then copies the app to `clusteraxe-<board>-<variant>.bin`. Building into a
dedicated dir with per‑variant defaults keeps configs from clobbering each other
and keeps the binary name honest.

**Verify a build before flashing** — a green exit code isn't enough:
- `grep CONFIG_CLUSTER_MODE build_<v>/config/sdkconfig.h` → the mode you expect
- the app `.bin` timestamp is from *this* build
- master (larger, carries slave‑coordination code) and slave images differ in size

---

## What changed since v1.5.2

- **Embedded web UI** — the app image now carries AxeOS; `www.bin` is retired.
- **Unlock Overclock Mode** button on Settings (no more `?oc` URL trick).
- **Live telemetry WebSocket**, **Best Shares scoreboard**, **last‑nonce Block
  Header**, and **task/CPU diagnostics**, adopted from ESP‑Miner 2.15.1.
- Six prebuilt variants (Gamma 601 + GammaTurbo 801 × master/slave/standalone),
  independently rebuilt and verified.
- Version bumped to **v1.6.0** — reported by the board's system info and web UI.

*ZombieOS — a rebranded, reanimated ESP‑Miner. Powered by the atom. Mine on.*
