# ClusterAxe

Custom firmware enabling Master/Slave clustering for Bitaxe devices. Transform multiple solo miners into a coordinated mining cluster with a single pool connection.

```
                    ┌─────────────────────┐
                    │    Mining Pool      │
                    └──────────┬──────────┘
                               │
                    ┌──────────▼──────────┐
                    │   MASTER BITAXE     │
                    │  • Pool Connection  │
                    │  • Work Distribution│
                    │  • Share Aggregation│
                    │  • Cluster Dashboard│
                    └──────────┬──────────┘
                               │ BAP Cable
            ┌──────────────────┼──────────────────┐
            │                  │                  │
    ┌───────▼───────┐  ┌───────▼───────┐  ┌───────▼───────┐
    │   SLAVE #1    │  │   SLAVE #2    │  │   SLAVE #N    │
    │  • Mining     │  │  • Mining     │  │  • Mining     │
    │  • Reporting  │  │  • Reporting  │  │  • Reporting  │
    └───────────────┘  └───────────────┘  └───────────────┘
```

## Features

- **Single Pool Connection** - Master handles all pool communication
- **Nonce Range Partitioning** - Zero duplicate work across cluster
- **Unified Dashboard** - Monitor all devices from master's web UI
- **Hot-Swap Support** - Add/remove slaves without restart
- **Individual Device Access** - Each slave retains full web UI
- **Open Protocol** - Uses BAP (Bitaxe Accessory Protocol) over UART

## Hardware Requirements

- 2+ Bitaxe devices (any variant with BAP header)
- BAP cable (TX/RX crossed, GND connected)
- WiFi network

## Quick Start

### 1. Flash Firmware

**Master device:**
```bash
esptool.py --chip esp32s3 --port COM3 --baud 460800 write_flash \
  -z --flash_mode dio --flash_freq 80m --flash_size detect \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 clusteraxe-master.bin \
  0x410000 www.bin
```

**Slave device(s):**
```bash
esptool.py --chip esp32s3 --port COM3 --baud 460800 write_flash \
  -z --flash_mode dio --flash_freq 80m --flash_size detect \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 clusteraxe-slave.bin \
  0x410000 www.bin
```

### 2. Wire BAP Cable

```
MASTER          SLAVE
──────          ─────
Pin 2 (TX)  ─── Pin 3 (RX)    [crossed]
Pin 3 (RX)  ─── Pin 2 (TX)    [crossed]
Pin 4 (GND) ─── Pin 4 (GND)   [required]
```

### 3. Configure

1. Connect both devices to same WiFi network
2. Configure pool settings on Master
3. Slaves auto-register when connected via BAP cable
4. Monitor cluster from Master's web UI → Cluster page

## Building from Source

### Prerequisites
- ESP-IDF v5.5.1
- Node.js v22+ (for web UI)

### Build Master
```bash
cd main/http_server/axe-os && npm install && npm run build && cd ../../..
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.master" build
```

### Build Slave
```bash
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.slave" fullclean build
```

## Documentation

- [User Guide](docs/USER_GUIDE.md) - Complete setup instructions
- [BAP Wiring](docs/BAP_WIRING.md) - Cable wiring reference
- [Testing Checklist](docs/TESTING_CHECKLIST.md) - Hardware testing guide
- [Firmware Roadmap](docs/FIRMWARE_ROADMAP.md) - Development status

## API Endpoints

### Master
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/cluster/status` | GET | Cluster status + all slave data |
| `/api/cluster/mode` | POST | Change cluster mode |

### Slave
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/system/info` | GET | Device information |
| `/api/swarm/info` | GET | Mining statistics |
| `/api/system/restart` | POST | Restart device |

## Specifications

| Parameter | Value |
|-----------|-------|
| Protocol | BAP (UART @ 115200 baud) |
| Message Format | NMEA-style ASCII |
| Max Slaves | 8 (default), 16 (configurable) |
| Heartbeat | 3 seconds |
| Timeout | 10 seconds |

## Status

**Development Phase:** Hardware Testing

- [x] Protocol design
- [x] Master firmware
- [x] Slave firmware
- [x] Web interface
- [x] API layer
- [x] Documentation
- [ ] Hardware validation
- [ ] Production release

## License

GPL-3.0 - See [LICENSE](LICENSE) for details.

## Credits

Based on [ESP-Miner](https://github.com/skot/ESP-Miner) by skot.

## Community

- [OSMU Discord](https://discord.gg/osmu)
- [GitHub Issues](https://github.com/ShaeOJ/ClusterAxe/issues)
