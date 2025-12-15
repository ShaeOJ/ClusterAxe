# ClusterAxe

**v1.0.0** | Open-source firmware enabling coordinated clustering for Bitaxe devices.

Transform multiple solo miners into a unified mining cluster with a single pool connection, eliminating duplicate work and providing centralized monitoring.

```
                    ┌─────────────────────┐
                    │    Mining Pool      │
                    └──────────┬──────────┘
                               │ Stratum
                    ┌──────────▼──────────┐
                    │   MASTER BITAXE     │
                    │  • Pool Connection  │
                    │  • Work Distribution│
                    │  • Share Aggregation│
                    │  • Cluster Dashboard│
                    └──────────┬──────────┘
                               │ ESP-NOW / BAP
            ┌──────────────────┼──────────────────┐
            │                  │                  │
    ┌───────▼───────┐  ┌───────▼───────┐  ┌───────▼───────┐
    │   SLAVE #1    │  │   SLAVE #2    │  │   SLAVE #N    │
    │  • Mining     │  │  • Mining     │  │  • Mining     │
    │  • Reporting  │  │  • Reporting  │  │  • Reporting  │
    └───────────────┘  └───────────────┘  └───────────────┘
```

## Features

- **Single Pool Connection** - Master handles all Stratum communication
- **Nonce Range Partitioning** - Zero duplicate work across cluster
- **ESP-NOW Wireless** - Sub-millisecond latency, no router required
- **BAP Wired Option** - RS-485 daisy-chain for reliable connectivity
- **Unified Dashboard** - Monitor all devices from master's web UI
- **Remote Configuration** - Adjust slave freq/voltage/fan from master
- **Hot-Swap Support** - Add/remove slaves without restart
- **Individual Device Access** - Each slave retains full web UI

## Communication Options

| Method | Range | Latency | Max Devices | Use Case |
|--------|-------|---------|-------------|----------|
| **ESP-NOW** | ~200m | <5ms | 20 | Wireless, easy setup |
| **BAP (RS-485)** | ~1200m | <10ms | 16 | Wired, industrial reliability |

## Hardware Requirements

- 2+ Bitaxe devices (any variant: Ultra, Supra, Gamma, Hex)
- WiFi network (for pool connection on master)
- **For wired:** BAP cable (TX/RX crossed, GND connected)
- **For wireless:** Nothing extra - ESP-NOW built-in

## Quick Start

### Option 1: Pre-built Binaries

Download from [Releases](https://github.com/ShaeOJ/ClusterAxe/releases):
- `clusteraxe-master.bin` - Flash to your coordinator device
- `clusteraxe-slave.bin` - Flash to worker devices
- `www.bin` - Web UI (same for both)

**Flash with esptool:**
```bash
esptool.py --chip esp32s3 --port COM3 --baud 460800 write_flash \
  -z --flash_mode dio --flash_freq 80m --flash_size detect \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 clusteraxe-master.bin \
  0x410000 www.bin
```

### Option 2: Web Flash

Use [ESP Web Tools](https://web.esphome.io/) to flash directly from browser.

### Setup

1. Flash master firmware to one device, slave firmware to others
2. Connect all devices to the same WiFi network
3. Configure pool settings on Master (Settings → Pool)
4. Go to Master UI → Cluster page
5. Set mode to "Master" and restart
6. Slaves auto-discover and connect via ESP-NOW

## Building from Source

### Prerequisites
- ESP-IDF v5.5.1
- Node.js v22+ (for web UI)

### Build Web UI
```bash
cd main/http_server/axe-os
npm install && npm run build
cd ../../..
```

### Build Master Firmware
```bash
idf.py set-target esp32s3
idf.py menuconfig  # Set Cluster Mode → Master
idf.py build
```

### Build Slave Firmware
```bash
idf.py menuconfig  # Set Cluster Mode → Slave
idf.py build
```

## API Endpoints

### Master Cluster API
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/cluster/status` | GET | Cluster status + all slave data |
| `/api/cluster/mode` | POST | Change cluster mode |
| `/api/cluster/slave/{id}/config` | GET | Get slave configuration |
| `/api/cluster/slave/{id}/setting` | POST | Update slave setting |
| `/api/cluster/slaves/setting` | POST | Update all slaves |

### Standard Device API
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/system/info` | GET | Device information |
| `/api/system/restart` | POST | Restart device |

## Specifications

| Parameter | Value |
|-----------|-------|
| ESP-NOW Data Rate | 1 Mbps |
| BAP Baud Rate | 115200 (default) |
| Message Format | Binary + NMEA-style |
| Max Cluster Size | 20 (ESP-NOW) / 16 (BAP) |
| Heartbeat Interval | 3 seconds |
| Device Timeout | 10 seconds |

## Documentation

- [ESP-NOW Setup](docs/ESP-NOW.md) - Wireless clustering guide
- [BAP Wiring](docs/BAP_WIRING.md) - Cable wiring reference
- [API Reference](docs/API.md) - Full API documentation
- [Defensive Publication](DEFENSIVE_PUBLICATION.md) - Prior art establishment

## Status

**Current Version:** v1.0.0

- [x] ESP-NOW wireless transport
- [x] BAP wired transport
- [x] Master/Slave firmware
- [x] Web UI with cluster dashboard
- [x] Remote slave configuration
- [x] Share aggregation
- [ ] Auto-tuning optimization
- [ ] Mesh networking for extended range

## Prior Art Notice

This project includes a [Defensive Publication](DEFENSIVE_PUBLICATION.md) establishing prior art for the described clustering methods. This prevents third-party patent claims while keeping the technology open for community use.

## License

GPL-3.0 - See [LICENSE](LICENSE) for details.

## Credits

- Based on [ESP-Miner](https://github.com/skot/ESP-Miner) by skot
- Bitaxe hardware by [OSMU](https://opensourceminers.org/)

## Community

- [OSMU Discord](https://discord.gg/osmu)
- [GitHub Issues](https://github.com/ShaeOJ/ClusterAxe/issues)
- [Bitaxe-IO Facebook](https://www.facebook.com/groups/bitaxe)

---

*ClusterAxe - Unite Your Hashrate*
