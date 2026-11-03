# ZombieOS 1.0 — Flashing Guide

ZombieOS is the Bitaxe/NerdAxe firmware formerly released as **ClusterAxe**. The
web UI is compiled into the app binary, so there is **no separate `www.bin`** to
flash.

> **Binary filenames still start with `clusteraxe-`** in this release so that
> existing devices can update over-the-air without breaking their board-safety
> check. The firmware itself identifies as **ZombieOS-v1.0.0** (shown on the
> screen, web UI, and `/api/system/info`). A later release will rename the asset
> files to `zombieos-…`.

---

## 1. Pick the right binary

Choose **one** file for your board and role:

| Board | Role | File |
|-------|------|------|
| Bitaxe Gamma (1× BM1370, "601") | Standalone | `clusteraxe-gamma601-standalone.bin` |
| Bitaxe Gamma (601) | Cluster master | `clusteraxe-gamma601-master.bin` |
| Bitaxe Gamma (601) | Cluster slave | `clusteraxe-gamma601-slave.bin` |
| GammaTurbo (2× BM1370, 12 V, "801") | Standalone | `clusteraxe-gt801-standalone.bin` |
| GammaTurbo (801) | Cluster master | `clusteraxe-gt801-master.bin` |
| GammaTurbo (801) | Cluster slave | `clusteraxe-gt801-slave.bin` |

- **Standalone** = a normal single miner (no cluster). Start here if unsure.
- **Master** = the one device that holds the pool connection and coordinates slaves.
- **Slave** = a device that mines work handed to it by the master.
- ⚠️ **Do not cross boards.** A 601 image on an 801 (or vice versa) uses the wrong
  voltage domain. Match the board exactly.

---

## 2. First time coming from stock AxeOS — rename to `esp-miner.bin`

Stock AxeOS only accepts a firmware file literally named **`esp-miner.bin`**.

1. Copy your chosen file and **rename the copy to `esp-miner.bin`**. For example:
   - `clusteraxe-gt801-standalone.bin` → **`esp-miner.bin`**
2. In the stock AxeOS web interface, open **Settings → Firmware Update** (or the
   update/OTA page).
3. Upload `esp-miner.bin` in the **firmware** slot and apply.
4. There is **nothing to upload in the website/www slot** — the UI is built in.
5. Let the device reboot. It should come up reporting **ZombieOS-v1.0.0**.

> Tip: keep the original board/role filename somewhere so you remember which
> image this `esp-miner.bin` actually is.

---

## 3. Updating from an existing ClusterAxe / ZombieOS device (OTA)

The in-device updater is board-aware. On the device's **Update** page:

- Upload the matching **`clusteraxe-<board>-<role>.bin`** for *this* device.
  The updater accepts it for the correct board and rejects the other board's
  image.
- If it ever refuses the board-named file, upload the raw **`zombie-os.bin`**
  from the same build instead (always accepted).
- No website/www upload is needed.

---

## 4. Serial flash with esptool (full, from scratch)

ESP32-S3, 16 MB flash, DIO @ 80 MHz. Run from the folder that contains the
build output (the `build_…` directory), using **that board/role's** app binary:

```
python -m esptool --chip esp32s3 -b 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0      bootloader/bootloader.bin \
  0x8000   partition_table/partition-table.bin \
  0x10000  <app>.bin \
  0xf10000 ota_data_initial.bin
```

Replace `<app>.bin` with your chosen image (e.g. `zombie-os.bin`, or the
renamed `clusteraxe-gt801-standalone.bin`). There is **no `www.bin`** line — the
old `0x410000 www.bin` step is gone as of the embedded-UI change.

---

## 5. After flashing

- The screen and web UI should read **ZombieOS-v1.0.0**.
- Standalone: set pool/wifi and mine as usual.
- Cluster: flash **one** master and the rest as slaves on the same board type,
  on the same network. The master coordinates work automatically.
- Overheat safety: if a device latches its fan to 100 % after an overheat event,
  set the fan back to **Auto** (or your preferred manual %) in the UI to clear it.
