# ClusterAxe

**Distributed Bitcoin Mining Firmware for BitAxe Devices**

Transform multiple BitAxe solo miners into a unified mining cluster with a single pool connection, eliminating duplicate work and providing centralized monitoring.

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
                               │ ESP-NOW (Wireless)
            ┌──────────────────┼──────────────────┐
            │                  │                  │
    ┌───────▼───────┐  ┌───────▼───────┐  ┌───────▼───────┐
    │   SLAVE #1    │  │   SLAVE #2    │  │   SLAVE #N    │
    │  • Mining     │  │  • Mining     │  │  • Mining     │
    │  • Reporting  │  │  • Reporting  │  │  • Reporting  │
    └───────────────┘  └───────────────┘  └───────────────┘
```

**Maximum Cluster Size**: 1 Master + 8 Slaves = 9 BitAxe units

## Features

- **Single Pool Connection** - Master handles all Stratum communication
- **Nonce Range Partitioning** - Zero duplicate work across cluster
- **ESP-NOW Wireless** - Sub-millisecond latency, no cables required (~200m range)
- **BAP Wired Option** - RS-485 daisy-chain for industrial reliability
- **Unified Dashboard** - Monitor all devices from master's web UI
- **Remote Configuration** - Adjust slave frequency/voltage/fan from master
- **Auto-Discovery** - Slaves automatically find and connect to master
- **Per-Slave Telemetry** - Hashrate, temperature, power, shares for each device
- **Hot-Swap Support** - Add/remove slaves without restart

## Supported Hardware

ClusterAxe supports all BitAxe variants:

| Family | ASIC | Board Versions | Typical Hashrate | Max Power |
|--------|------|----------------|------------------|-----------|
| **Max** | BM1397 | 2.2, 102 | ~400 GH/s | 25W |
| **Ultra** | BM1366 | 0.11, 201-205, 207 | ~500 GH/s | 25W |
| **Hex** | BM1366 x6 | 302, 303 | ~3 TH/s | 90W |
| **Supra** | BM1368 | 400-403 | ~600 GH/s | 40W |
| **Gamma** | BM1370 | 600-602 | ~1.2 TH/s | 40W |
| **SupraHex** | BM1368 x6 | 701, 702 | ~3.6 TH/s | 120W |
| **GammaTurbo** | BM1370 x2 | 800 | ~2.4 TH/s | 60W |

## Communication Options

| Method | Range | Latency | Max Devices | Use Case |
|--------|-------|---------|-------------|----------|
| **ESP-NOW** | ~200m | <5ms | 20 | Wireless, easy setup |
| **BAP (RS-485)** | ~1200m | <10ms | 16 | Wired, industrial reliability |

---

## Quick Start with Pre-built Binaries

Download from [Releases](https://github.com/ShaeOJ/ClusterAxe/releases):
- `clusteraxe-master.bin` - Flash to your coordinator device
- `clusteraxe-slave.bin` - Flash to worker devices

### Flash with esptool

```bash
# Install esptool if needed
pip install esptool

# Flash Master (replace COM3 with your port)
esptool.py --chip esp32s3 --port COM3 --baud 460800 write_flash \
  -z --flash_mode dio --flash_freq 80m --flash_size detect \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 clusteraxe-master.bin \
  0x410000 www.bin

# Flash Slave
esptool.py --chip esp32s3 --port COM4 --baud 460800 write_flash \
  -z --flash_mode dio --flash_freq 80m --flash_size detect \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 clusteraxe-slave.bin \
  0x410000 www.bin
```

---

## Building from Source

### Prerequisites

#### ESP-IDF (Required)

ESP-IDF v5.5.1 or later is required. Follow the official installation guide for your platform:

**Windows:**
1. Download and run the [ESP-IDF Windows Installer](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/windows-setup.html)
2. Select ESP-IDF v5.5.1 during installation
3. Use "ESP-IDF PowerShell" or "ESP-IDF Command Prompt" for all build commands

**Linux:**
```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv \
  cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0

# Clone and install ESP-IDF
mkdir -p ~/esp
cd ~/esp
git clone -b v5.5.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3

# Add to your shell profile (~/.bashrc or ~/.zshrc)
alias get_idf='. ~/esp/esp-idf/export.sh'
```

**macOS:**
```bash
# Install Xcode command line tools
xcode-select --install

# Clone and install ESP-IDF
mkdir -p ~/esp
cd ~/esp
git clone -b v5.5.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3

# Add to ~/.zshrc
alias get_idf='. ~/esp/esp-idf/export.sh'
```

#### Node.js (Required for Web UI)

Node.js v18+ is required to build the web interface.

**Windows:** Download from [nodejs.org](https://nodejs.org/)

**Linux:**
```bash
curl -fsSL https://deb.nodesource.com/setup_18.x | sudo -E bash -
sudo apt-get install -y nodejs
```

**macOS:**
```bash
brew install node
```

### Clone the Repository

```bash
git clone https://github.com/ShaeOJ/ClusterAxe.git
cd ClusterAxe
```

### Build Master Firmware

The master device handles pool connections and coordinates all slaves.

```bash
# Activate ESP-IDF environment (Linux/macOS)
get_idf
# Or on Windows, use ESP-IDF Command Prompt

# Copy master configuration
cp sdkconfig.master sdkconfig

# Build the firmware (this also builds the web UI automatically)
idf.py build

# Flash to device (replace COM3 with your actual port)
# Windows: COM3, COM4, etc.
# Linux: /dev/ttyUSB0, /dev/ttyACM0, etc.
# macOS: /dev/cu.usbserial-*, /dev/cu.SLAB_USBtoUART, etc.
idf.py -p COM3 flash

# Optional: Monitor serial output for debugging
idf.py -p COM3 monitor
# Press Ctrl+] to exit monitor
```

### Build Slave Firmware

Slave devices receive work from the master and report shares back.

```bash
# Clean previous build (recommended when switching configurations)
idf.py fullclean

# Copy slave configuration
cp sdkconfig.slave sdkconfig

# Build
idf.py build

# Flash to slave device
idf.py -p COM4 flash
```

### One-Line Build Commands

```bash
# Master: clean, configure, build, and flash
idf.py fullclean && cp sdkconfig.master sdkconfig && idf.py build flash -p COM3

# Slave: clean, configure, build, and flash
idf.py fullclean && cp sdkconfig.slave sdkconfig && idf.py build flash -p COM4
```

### Building the Web UI Separately

The web UI is built automatically during firmware compilation. To build it manually:

```bash
cd main/http_server/axe-os
npm install
npm run build
cd ../../..
```

---

## Configuration

### Build-Time Options

Run `idf.py menuconfig` and navigate to **ClusterAxe Configuration**:

| Option | Default | Description |
|--------|---------|-------------|
| Build as Cluster Master | n | Enable for the coordinator device |
| Build as Cluster Slave | n | Enable for worker devices |
| Maximum number of slaves | 8 | Max slaves the master can manage |
| Heartbeat interval (ms) | 3000 | How often slaves report status |
| Slave timeout (ms) | 10000 | Time before slave marked offline |
| Cluster Transport | ESP-NOW | Communication method |
| ESP-NOW WiFi Channel | 1 | Must match across all devices |
| Enable Automatic Pairing | y | Slaves auto-connect to master |

### Runtime Setup

After flashing, each device creates a WiFi access point:

1. **Connect to device AP**: `ClusterAxe_XXXXXX` (no password)
2. **Open browser**: Navigate to `http://192.168.4.1`
3. **Configure WiFi**: Settings → WiFi → Enter your network credentials
4. **Device restarts** and connects to your network

#### Master Configuration

1. Go to **Settings → Pool**
2. Enter your pool details:
   - **Stratum URL**: e.g., `public-pool.io`
   - **Port**: e.g., `21496`
   - **Worker**: Your BTC address (e.g., `bc1q...bitaxe`)
3. The master begins broadcasting discovery beacons automatically

#### Slave Configuration

1. Configure WiFi only (no pool settings needed)
2. The slave automatically:
   - Discovers the master via ESP-NOW beacons
   - Registers and receives a slave ID
   - Begins receiving work and mining

---

## Web Interface

### Master Dashboard

Access at `http://<master-ip>/` to view:

- **Cluster Overview**: Total hashrate, power, efficiency, share counts
- **Master Stats**: Local hashrate, temperature, ASIC frequency/voltage
- **Slave Table**: Real-time stats for each connected slave
  - Hashrate (current, 1m, 10m averages)
  - Temperature and fan speed
  - Frequency and voltage settings
  - Power consumption
  - Accepted/rejected shares
  - Connection status and last seen time

### Slave Dashboard

Each slave retains its own web interface showing:
- Local hashrate and mining stats
- Temperature and power monitoring
- Connection status to master
- Assigned slave ID

---

## API Reference

### Master Cluster API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/cluster/status` | GET | Full cluster status with all slave data |
| `/api/cluster/mode` | POST | Change cluster mode (requires restart) |
| `/api/cluster/slave/{id}/config` | GET | Get slave configuration |
| `/api/cluster/slave/{id}/frequency` | POST | Set slave ASIC frequency |
| `/api/cluster/slave/{id}/voltage` | POST | Set slave core voltage |
| `/api/cluster/slave/{id}/fanspeed` | POST | Set slave fan speed |
| `/api/cluster/slave/{id}/restart` | POST | Restart specific slave |
| `/api/cluster/slave/{id}/identify` | POST | Flash slave LED for identification |
| `/api/cluster/slaves/setting` | POST | Apply setting to all slaves |
| `/api/cluster/slaves/restart` | POST | Restart all slaves |

### Standard Device API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/system/info` | GET | Device info, hashrate, temperature |
| `/api/system/restart` | POST | Restart the device |

---

## Troubleshooting

### Slave Not Connecting to Master

1. **Check WiFi**: Both devices must be on the same network
2. **Check ESP-NOW Channel**: Must match (default: channel 1)
3. **Check Serial Logs**: `idf.py -p COMx monitor`
4. **Restart Both**: Power cycle master first, then slaves

### No Shares Being Accepted

1. **Verify Pool Settings**: Check URL, port, and worker name on master
2. **Check Pool Connection**: Master dashboard shows connection status
3. **Monitor Difficulty**: Ensure pool difficulty matches your hashrate

### Web UI Not Loading

1. **Clear Browser Cache**: Ctrl+Shift+R or Cmd+Shift+R
2. **Check IP Address**: Verify device IP in your router's client list
3. **Try Direct IP**: Use `http://192.168.x.x` instead of hostname

### High Stale/Rejected Rate

1. **Check Network Latency**: High ping to pool increases stales
2. **Reduce Cluster Size**: More slaves = more coordination overhead
3. **Use Closer Pool**: Select geographically nearby pool server

---

## Project Structure

```
ClusterAxe/
├── main/
│   ├── cluster/                    # Cluster implementation
│   │   ├── cluster.c/h             # Core logic and API
│   │   ├── cluster_master.c        # Master coordination
│   │   ├── cluster_slave.c         # Slave work processing
│   │   ├── cluster_espnow.c/h      # ESP-NOW transport
│   │   ├── cluster_protocol.h      # Message definitions
│   │   ├── cluster_integration.c/h # System integration
│   │   └── Kconfig.projbuild       # Build configuration
│   ├── http_server/
│   │   ├── http_server.c           # REST API handlers
│   │   └── axe-os/                 # Angular web UI
│   ├── tasks/                      # FreeRTOS tasks
│   │   ├── stratum_task.c          # Pool communication
│   │   ├── asic_task.c             # ASIC control
│   │   └── hashrate_monitor_task.c # Stats collection
│   └── ...
├── components/                     # ESP-IDF components
│   ├── asic/                       # ASIC drivers (BM1366, BM1368, etc.)
│   ├── stratum/                    # Stratum protocol
│   └── connect/                    # WiFi management
├── sdkconfig.master                # Master build config
├── sdkconfig.slave                 # Slave build config
├── sdkconfig.defaults              # Default ESP-IDF settings
└── CMakeLists.txt                  # Build configuration
```

---

## Contributing

1. Fork the repository
2. Create a feature branch
3. Test with at least 1 master + 1 slave
4. Verify shares are accepted by pool
5. Submit a pull request

---

## License

GPL-3.0 License - See [LICENSE](LICENSE) for details.

## Credits

- Based on [ESP-Miner](https://github.com/skot/ESP-Miner) by skot
- BitAxe hardware by [OSMU](https://opensourceminers.org/)
- ESP-NOW protocol by Espressif

## Community

- [OSMU Discord](https://discord.gg/osmu)
- [GitHub Issues](https://github.com/ShaeOJ/ClusterAxe/issues)
- [Bitaxe Facebook Group](https://www.facebook.com/groups/bitaxe)

---

*ClusterAxe - Unite Your Hashrate*
