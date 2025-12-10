# Clusteraxe User Guide

## Overview

Clusteraxe enables multiple Bitaxe devices to operate as a coordinated mining cluster. One device acts as the **Master** (maintains pool connection, distributes work) while others act as **Slaves** (receive work, report shares back).

**Benefits:**
- Single pool connection for multiple miners
- Coordinated nonce range distribution (no duplicate work)
- Centralized monitoring from master dashboard
- Each slave retains its own web UI for individual configuration

---

## Hardware Requirements

- 2 or more Bitaxe devices (any variant with BAP header)
- Custom BAP cable(s) for interconnection
- Adequate power supply for all devices
- WiFi network access

---

## BAP Cable Wiring

The BAP (Bitaxe Accessory Protocol) header is a 4-pin connector used for UART communication between devices.

### Pinout

| Pin | Signal | Description |
|-----|--------|-------------|
| 1 | 5V | Power (optional, not used for cluster) |
| 2 | TX | Transmit Data |
| 3 | RX | Receive Data |
| 4 | GND | Ground |

### Master-to-Slave Cable Wiring

```
MASTER                          SLAVE
┌──────────────┐               ┌──────────────┐
│ BAP Header   │               │ BAP Header   │
├──────────────┤               ├──────────────┤
│ Pin 1: 5V    │───────────────│ Pin 1: 5V    │  (Optional - don't connect if using separate power)
│ Pin 2: TX    │───────────────│ Pin 3: RX    │  (Cross-connect: Master TX → Slave RX)
│ Pin 3: RX    │───────────────│ Pin 2: TX    │  (Cross-connect: Master RX ← Slave TX)
│ Pin 4: GND   │───────────────│ Pin 4: GND   │  (Common ground - REQUIRED)
└──────────────┘               └──────────────┘
```

### Important Wiring Notes

1. **TX/RX are crossed** - Master TX connects to Slave RX and vice versa
2. **GND is required** - Always connect ground between devices
3. **5V is optional** - Only connect if powering slave from master (not recommended)
4. **Keep cables short** - Under 30cm recommended for reliable communication
5. **Use shielded cable** - Reduces interference in noisy environments

### Cable Diagram (Visual)

```
Master BAP          4-Wire Cable           Slave BAP
    ┌─┐                                       ┌─┐
 1  │●│─────────── 5V (optional) ────────────│●│  1
    ├─┤                    ╲                  ├─┤
 2  │●│─────────── TX ──────╲────────────────│●│  3  (RX)
    ├─┤                      ╲                ├─┤
 3  │●│─────────── RX ────────╲──────────────│●│  2  (TX)
    ├─┤                                       ├─┤
 4  │●│─────────── GND ──────────────────────│●│  4
    └─┘                                       └─┘
```

---

## Multi-Slave Topologies

### Option 1: Daisy Chain (Simple, Limited)

```
Master ──BAP──> Slave 1 ──BAP──> Slave 2 ──BAP──> Slave 3
```

**Note:** Daisy chain requires signal buffering for more than 2-3 slaves. Not recommended for large clusters.

### Option 2: Star Topology (Recommended)

```
                    ┌──────────> Slave 1
                    │
Master ──BAP──> Hub ├──────────> Slave 2
                    │
                    └──────────> Slave 3
```

Use a UART hub/splitter that buffers signals. This provides the most reliable communication.

### Option 3: RS-485 Bus (Future)

For clusters larger than 8 devices, RS-485 bus topology is planned for a future release.

---

## Firmware Installation

### Step 1: Download Firmware

Download the pre-built firmware files:
- `clusteraxe-master.bin` - For the master device
- `clusteraxe-slave.bin` - For slave devices
- `bootloader.bin` - Bootloader (same for both)
- `partition-table.bin` - Partition table (same for both)
- `www.bin` - Web UI (same for both)

### Step 2: Flash Master Device

Using esptool (replace COM3 with your port):

```bash
esptool.py --chip esp32s3 --port COM3 --baud 460800 \
  --before default_reset --after hard_reset write_flash \
  -z --flash_mode dio --flash_freq 80m --flash_size detect \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 clusteraxe-master.bin \
  0x410000 www.bin
```

Or use the Bitaxe web UI OTA update feature.

### Step 3: Flash Slave Device(s)

Same process but use `clusteraxe-slave.bin`:

```bash
esptool.py --chip esp32s3 --port COM3 --baud 460800 \
  --before default_reset --after hard_reset write_flash \
  -z --flash_mode dio --flash_freq 80m --flash_size detect \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 clusteraxe-slave.bin \
  0x410000 www.bin
```

---

## Configuration

### Master Setup

1. Connect to master's WiFi AP or access via IP address
2. Navigate to **Settings** and configure:
   - WiFi credentials
   - Pool URL and worker name
   - Mining settings (frequency, voltage, fan)
3. Navigate to **Cluster** page
4. Verify mode shows **Master**
5. Wait for slaves to connect

### Slave Setup

1. Connect to slave's WiFi AP or access via IP address
2. Navigate to **Settings** and configure:
   - WiFi credentials (same network as master)
   - Mining settings (frequency, voltage, fan)
   - Hostname (unique name for identification)
3. Navigate to **Cluster** page
4. Verify mode shows **Slave**
5. Connect BAP cable to master
6. Slave will auto-register with master

### Changing Cluster Mode

To change a device's cluster mode:

1. Go to **Cluster** page
2. Select new mode from dropdown (Disabled/Master/Slave)
3. Confirm the change
4. Device will restart with new mode

---

## Monitoring

### Master Dashboard

The master's Cluster page shows:

| Metric | Description |
|--------|-------------|
| Active Slaves | Number of connected slave devices |
| Cluster Hashrate | Combined hashrate of all devices |
| Shares Accepted | Total shares accepted by pool |
| Shares Rejected | Total shares rejected by pool |

**Slave Table columns:**
- Status (Active/Stale/Disconnected)
- Device (hostname + clickable IP link)
- Hashrate
- Temperature
- Fan RPM
- Frequency
- Voltage
- Power
- Shares
- Last Seen

### Accessing Slave Web UI

Click on any slave's IP address in the master dashboard to open that slave's web interface in a new tab. Each slave retains full functionality for:
- Viewing local statistics
- Adjusting frequency/voltage/fan settings
- Viewing logs
- Performing OTA updates

---

## API Access

Both master and slave devices expose REST APIs for external monitoring and control.

### Master API

```bash
# Get cluster status (includes all slave data)
curl http://<master-ip>/api/cluster/status

# Change cluster mode
curl -X POST http://<master-ip>/api/cluster/mode \
  -H "Content-Type: application/json" \
  -d '{"mode": 1}'  # 0=disabled, 1=master, 2=slave
```

### Slave API

```bash
# Get device info
curl http://<slave-ip>/api/system/info

# Get mining stats
curl http://<slave-ip>/api/swarm/info

# Change settings
curl -X POST http://<slave-ip>/api/system/settings \
  -H "Content-Type: application/json" \
  -d '{"frequency": 550, "coreVoltage": 1200}'

# Restart device
curl -X POST http://<slave-ip>/api/system/restart
```

---

## Troubleshooting

### Slave Not Connecting

1. **Check BAP cable** - Verify TX/RX are crossed, GND is connected
2. **Check WiFi** - Both devices must be on same network
3. **Check mode** - Verify slave is in Slave mode, master in Master mode
4. **Check logs** - Access device logs via web UI for error messages
5. **Restart devices** - Power cycle both master and slave

### Slave Shows "Stale"

- Slave missed heartbeats (communication issue)
- Check BAP cable connection
- Reduce cable length if possible
- Slave will recover automatically when communication resumes

### No Hashrate on Slave

1. Verify slave received work (check logs)
2. Check ASIC settings (frequency, voltage)
3. Ensure master has valid pool connection

### Shares Not Being Accepted

1. Check pool connection on master
2. Verify pool credentials
3. Check master logs for share submission errors

---

## LED Status Indicators

| LED Pattern | Meaning |
|-------------|---------|
| Solid Green | Normal operation, mining |
| Blinking Green | Receiving/sending data |
| Solid Orange | Connecting to WiFi/Master |
| Blinking Red | Error condition |

---

## Specifications

| Parameter | Value |
|-----------|-------|
| Protocol | BAP (UART @ 115200 baud) |
| Max Slaves | 8 (default), 16 (configurable) |
| Heartbeat Interval | 3 seconds |
| Timeout | 10 seconds |
| Message Format | NMEA-style ($CLXXX,...*XX) |

---

## Safety Notes

1. **Power** - Ensure adequate power supply for all devices
2. **Heat** - Provide sufficient cooling for clustered devices
3. **Cables** - Use quality cables to prevent communication errors
4. **Updates** - Keep all devices on same firmware version

---

## Support

- **Issues**: Report bugs at https://github.com/anthropics/claude-code/issues
- **Community**: Join Bitaxe Discord for community support

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | 2024 | Initial release |

