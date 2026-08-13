```
 ▂▃▅▇█▓▒░  Z O M B I E   O S  ░▒▓█▇▅▃▂
        C L U S T E R A X E   ·   v1.5.1
     reanimated firmware for BM1370 BitAxe
```

# ClusterAxe / ZombieOS v1.5.1 — Gamma + GammaTurbo release

Distributed BitAxe mining firmware for ESP32‑S3. A **master** connects to the
pool and hands work to up to 8 **slaves** over ESP‑NOW (wireless) or BAP/RS‑485
(wired). This release ships prebuilt **master** and **slave** images for both
the single‑chip **Gamma (board 601)** and the dual‑chip **GammaTurbo (board 801)**.

---

## Firmware manifest

| Board | Role | File | Size |
|-------|------|------|------|
| Gamma 601 (1× BM1370) | **Master** | `build_master/clusteraxe-gamma601-master.bin` | 1,464,224 B |
| Gamma 601 (1× BM1370) | **Slave**  | `build_slave/clusteraxe-gamma601-slave.bin`   | 1,336,928 B |
| GammaTurbo 801 (2× BM1370, 12 V) | **Master** | `build_gt_master/clusteraxe-gt801-master.bin` | 1,464,224 B |
| GammaTurbo 801 (2× BM1370, 12 V) | **Slave**  | `build_gt_slave/clusteraxe-gt801-slave.bin`   | 1,336,928 B |

Each `build_*/` directory is a **complete flashable set**: `bootloader/bootloader.bin`,
`partition_table/partition-table.bin`, the app image (`zombie-os-master.bin` /
`zombie-os-slave.bin`, also copied to the `clusteraxe-*` name above), `www.bin`
(web UI), and `ota_data_initial.bin`.

> **One master per cluster.** Flash exactly one board as master; flash every
> other board as slave. Master and slave are **not** interchangeable images.

---

## ⚠️ Flashing from a stock BitAxe (AxeOS OTA) — READ THIS FIRST

The AxeOS web updater accepts files by **fixed name**. If you flash over the
air from a stock BitAxe, you must **rename the app image to `esp-miner.bin`**
before uploading — AxeOS ignores any other filename.

1. Open the stock miner's web page → **Settings → Firmware Update**.
2. **Firmware slot:** take the app image for your board/role, e.g.
   `clusteraxe-gt801-master.bin`, and **rename it to `esp-miner.bin`**. Upload it.
3. **Website slot:** upload `www.bin` from the same `build_*/` folder (no rename).
4. Reboot. The board comes up as ZombieOS.

OTA only reflashes the app + web UI (not the bootloader/partition table), which
is exactly what you want coming from stock AxeOS — the partition layout already
matches.

---

## Clean flash with esptool (recommended for first‑time / recovery)

From inside the chosen `build_*/` directory:

```
python -m esptool --chip esp32s3 -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0      bootloader/bootloader.bin \
  0x8000   partition_table/partition-table.bin \
  0x10000  zombie-os-master.bin \
  0x410000 www.bin \
  0xf10000 ota_data_initial.bin
```

Replace `zombie-os-master.bin` with `zombie-os-slave.bin` for a slave. On a
Windows machine with ESP‑IDF set up you can also just run
`idf.py -B build_gt_master -p COMx flash`.

| Offset | Contents |
|--------|----------|
| `0x0` | bootloader |
| `0x8000` | partition table |
| `0x10000` | application (ZombieOS) |
| `0x410000` | `www.bin` — web UI |
| `0xf10000` | OTA data |

Target: **ESP32‑S3**, 16 MB flash, DIO @ 80 MHz.

---

## Building from source

Requires **ESP‑IDF 5.5.1** and **Node.js 22+** (the web UI builds as part of the
firmware). Each variant builds into its **own** directory so nothing overwrites:

```
build_master.bat       ->  build_master/       Gamma 601 master
build_slave.bat        ->  build_slave/        Gamma 601 slave
build_gt_master.bat    ->  build_gt_master/    GammaTurbo 801 master
build_gt_slave.bat     ->  build_gt_slave/     GammaTurbo 801 slave
build_gt_standalone.bat->  build/              GammaTurbo 801, no cluster
```

Under the hood each script runs
`idf.py -B <dir> -D SDKCONFIG=sdkconfig.<variant> build`. Building into a
dedicated dir with an explicit `SDKCONFIG` avoids two traps: Windows `copy`
preserves the source mtime (so copying a config over the shared `sdkconfig`
left ninja thinking nothing changed and it never reconfigured), and the project
name is taken from the config actually in use (so a master build can't emit a
slave‑named binary).

**Verify a build before flashing** — a green exit code isn't enough:
- `grep CONFIG_CLUSTER_MODE build_<v>/config/sdkconfig.h` → the mode you expect
- the app `.bin` timestamp is from *this* build
- master (~1.46 MB) and slave (~1.34 MB) images differ in size

---

## What's in v1.5.1

- Cluster‑wide best‑difficulty tracking, benchmark tool, and the always‑on
  watchdog + auto‑timing.
- Upstream ESP‑Miner fixes cherry‑picked through 2.14.2.
- Four prebuilt variants (Gamma 601 + GammaTurbo 801 × master/slave) with
  correctly‑named, independently‑verified images.

*ZombieOS — a rebranded, reanimated ESP‑Miner. Mine on.*
