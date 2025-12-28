# ClusterAxe Development Status & Roadmap

## Project Overview

ClusterAxe is a distributed Bitcoin mining firmware for BitAxe devices. It enables multiple BitAxe units to operate as a coordinated cluster, sharing a single stratum connection and distributing work efficiently.

**Architecture:**
- **Master**: Maintains stratum connection, distributes work to slaves, submits all shares to pool
- **Slave**: Receives work via ESP-NOW, mines with ASIC, reports shares back to master
- **Pool View**: Sees one worker with combined hashrate from all cluster nodes

**Max Cluster Size**: 1 Master + 8 Slaves = 9 BitAxe units

**Repository**: https://github.com/ShaeOJ/ClusterAxe (Public)

---

## Current Release

**v1.0.0** - Initial public release
- Pre-built binaries available on [GitHub Releases](https://github.com/ShaeOJ/ClusterAxe/releases)
- ESP-NOW wireless transport
- Full cluster dashboard with remote slave management

---

## Completed Features

### Core Cluster Functionality
- [x] ESP-NOW wireless transport layer for master/slave communication
- [x] Master discovery beacon broadcasting (slaves auto-discover master)
- [x] Slave registration and ID assignment
- [x] Work distribution from master to slaves with unique extranonce2 per slave
- [x] Nonce range partitioning to avoid duplicate work
- [x] Share submission from slaves to master via ESP-NOW
- [x] Master forwards slave shares to pool
- [x] Heartbeat system with extended telemetry (hashrate, temp, fan, power, voltage)

### Protocol Implementation
- [x] NMEA-style cluster messages ($CLWRK, $CLSHR, $CLHBT, $CLREG, $CLACK, etc.)
- [x] Version rolling support (4 midstates) passed from master to slave
- [x] Pool difficulty threshold forwarded to slaves
- [x] Job ID mapping for share submission

### Reliability Improvements
- [x] Retry logic for slave share transmission (3 attempts, 30ms delays)
- [x] Work re-broadcast to slaves that haven't received recent work
- [x] Slave reconnection handling after WiFi drops
- [x] Duplicate share detection on master

### Remote Configuration (Phase 2 - COMPLETED)
- [x] Master can remotely configure slave frequency
- [x] Master can remotely configure slave voltage
- [x] Master can remotely configure slave fan speed/mode
- [x] Master can restart individual slaves
- [x] Master can identify slaves (LED flash)
- [x] Bulk actions: set all slaves frequency/voltage/fan at once
- [x] Bulk restart all slaves

### Auto-Tuning (Phase 2 - COMPLETED)
- [x] Master auto-tune for optimal efficiency (J/TH optimization)
- [x] Slave auto-tune controlled from master dashboard
- [x] Enable/disable autotune per slave
- [x] Enable all slaves autotune at once
- [x] Autotune status display with oscilloscope-style animation
- [x] NVS persistence for autotune settings (survives reboot)
- [x] Visual indicators: hashrate bars, thermometer, power bolt icons

### Web UI
- [x] Master cluster dashboard showing all connected slaves
- [x] Per-slave stats: hashrate, temperature, fan RPM, shares, frequency, voltage, power
- [x] Slave dashboard showing cluster connection status
- [x] Real-time updates via polling
- [x] Master autotune section with same visual style as slaves
- [x] Oscilloscope/Pip-Boy style autotune animation
- [x] Transport type indicators (ESP-NOW wireless / BAP wired)

### Bug Fixes
- [x] **Extranonce2 change detection**: Fixed slave worker_task to detect new work based on extranonce2 changes
- [x] **Share counter tracking**: Fixed dashboard to show accepted/rejected shares per slave
- [x] **Voltage display mismatch**: Fixed to read from NVS (configured) instead of measured voltage
- [x] **Autotune NVS persistence**: Settings now saved to NVS and persist across reboots
- [x] **Slave hashrate graph spikes**: Fixed by using individual slave sums instead of total

### Release & Documentation
- [x] Repository made public on GitHub
- [x] Comprehensive README with build instructions
- [x] v1.0.0 release with pre-built binaries
- [x] Vault-Tec themed release notes

---

## Known Issues / In Progress

### Potential Issues
- [ ] Thread safety on pending_cluster_shares array (minor race condition possible)
- [ ] Slave timeout detection could be more aggressive
- [ ] No persistence of slave stats across master reboot

---

## Roadmap

### Phase 1: Stability & Polish (Short Term) - MOSTLY COMPLETE

#### Branding & Firmware Naming
- [x] **Change AxeOS logo to ClusterAxe** throughout UI
- [ ] **Fix firmware version mismatch issue**
- [x] **Rename firmware binaries**: `clusteraxe-master.bin` / `clusteraxe-slave.bin`
- [x] Update OTA logic to accept ClusterAxe filenames
- [ ] Update version strings and identifiers throughout codebase

#### Master UI Improvements
- [x] **Amalgamate Dashboard + Cluster into single "Cluster Dashboard"**
  - Show Master's own hashrate separately
  - Show Cluster total hashrate (master + all slaves combined)
  - Show cluster power, shares, active slaves
- [ ] **Remove/Disable Swarm feature** - still in menu, not needed for ClusterAxe
- [ ] **Update system for OTA**:
  - Pull firmware updates from ClusterAxe GitHub repo (not ESP-Miner)
  - Master should be able to push OTA updates to slaves
- [ ] Sortable slave table (by hashrate, temp, shares, etc.)

#### Slave UI Simplification
- [ ] **Simplify the menu** for slave operation
  - Remove unnecessary menu items
  - Keep it minimal: Dashboard, Settings, API
- [ ] **Remove/Disable Pool settings** - master handles pool connection
- [ ] **Hide WiFi settings after first setup** - only needed once

#### Communication Optimization
- [ ] **Investigate ways to increase ESP-NOW speed/throughput**
  - Reduce message overhead
  - Optimize packet sizes
  - Consider message batching for shares
- [ ] Measure and log communication latency for debugging

#### Reliability
- [ ] Improve ESP-NOW channel synchronization after WiFi reconnect
- [ ] Add configurable retry counts and timeouts
- [ ] Better handling of master reboot (slaves should re-register automatically)
- [ ] Watchdog for stuck states

### Phase 2: Enhanced Features (Medium Term) - PARTIALLY COMPLETE

#### Configuration
- [ ] Web UI to switch between Master/Slave/Disabled modes (currently compile-time)
- [ ] Configurable cluster ID to support multiple clusters on same network
- [x] Master can remotely configure slave settings (frequency, voltage, fan)

#### Auto-Tuning - COMPLETED
- [x] Master auto-tune for optimal efficiency
- [x] Slave auto-tune from master dashboard
- [x] Support for efficiency optimization (J/TH)
- [ ] **Cluster-wide thermal management**
  - Monitor all slave temperatures
  - Throttle slaves approaching thermal limits

#### Monitoring & Stats
- [ ] Historical hashrate graphs per slave
- [ ] Share submission latency tracking
- [ ] ESP-NOW packet loss statistics
- [x] Power efficiency metrics (J/TH) per slave and cluster total
- [ ] Export stats to CSV/JSON

#### Advanced Work Distribution
- [ ] Dynamic nonce range adjustment based on slave hashrate
- [ ] Prioritize work distribution to faster slaves
- [ ] Load balancing based on slave performance

### Phase 3: Advanced Features (Long Term)

#### Multi-Master Support
- [ ] Failover to backup master if primary goes offline
- [ ] Master election protocol

#### Pool Features
- [ ] Support for multiple pools (master handles failover)
- [ ] Per-slave pool assignment (different workers)
- [ ] Solo mining mode support

#### Hardware Integration
- [ ] Support for different ASIC types in same cluster
- [ ] Automatic frequency/voltage tuning based on cluster thermal budget
- [ ] Fan curve coordination across cluster

#### Mobile & Remote
- [ ] Mobile app for cluster monitoring
- [ ] Remote access via cloud relay
- [ ] Push notifications for alerts (slave offline, overheat, etc.)

---

## File Structure

```
main/cluster/
├── cluster.h              # Main header with types and API
├── cluster.c              # Core cluster logic and message routing
├── cluster_config.h       # Compile-time configuration
├── cluster_master.c       # Master-specific implementation
├── cluster_slave.c        # Slave-specific implementation
├── cluster_espnow.c       # ESP-NOW transport layer
├── cluster_espnow.h       # ESP-NOW header
├── cluster_integration.c  # Integration with ESP-Miner (stratum, ASIC)
├── cluster_integration.h  # Integration header
├── cluster_autotune.c     # Auto-tuning algorithm
├── cluster_autotune.h     # Auto-tuning header
├── cluster_protocol.h     # Protocol message definitions
├── cluster_remote_config.c # Remote configuration protocol
├── cluster_remote_config.h # Remote configuration header
└── Kconfig.projbuild      # Kconfig menu options

main/http_server/axe-os/src/app/
├── components/cluster/    # Angular cluster dashboard component
└── services/cluster.service.ts  # Cluster API service
```

---

## Build Instructions

### Master Firmware
```bash
cp sdkconfig.master sdkconfig
idf.py build
idf.py -p COM[X] flash
```

### Slave Firmware
```bash
cp sdkconfig.slave sdkconfig
idf.py build
idf.py -p COM[X] flash
```

---

## Configuration Options

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_CLUSTER_MAX_SLAVES` | 8 | Maximum slaves per master |
| `CONFIG_CLUSTER_WORK_QUEUE_SIZE` | 4 | Work queue depth |
| `CONFIG_CLUSTER_SHARE_QUEUE_SIZE` | 16 | Share queue depth |
| `CONFIG_CLUSTER_HEARTBEAT_MS` | 3000 | Heartbeat interval |
| `CONFIG_CLUSTER_TIMEOUT_MS` | 10000 | Slave timeout threshold |
| `CONFIG_CLUSTER_TRANSPORT_ESPNOW` | y | Use ESP-NOW wireless |
| `CONFIG_CLUSTER_ESPNOW_AUTO_PAIR` | y | Auto-discover and pair |

---

## Contributing

When making changes:
1. Test with at least 1 master + 1 slave setup
2. Monitor serial logs on both devices
3. Verify shares are accepted by pool
4. Check dashboard updates correctly
5. Test WiFi reconnection scenarios

---

## Session Notes

### 2024-12-27: v1.0.0 Release
- Repository made public
- Created v1.0.0 release with pre-built binaries
- Added comprehensive README with build instructions
- Vault-Tec themed release notes

### 2024-12-27: Master Autotune & UI Improvements
- Added master autotune with efficiency optimization
- Fixed voltage display mismatch (NVS vs measured)
- Added NVS persistence for autotune settings
- Added oscilloscope/Pip-Boy style animation
- Added visual icons (hashrate bars, thermometer, power bolt) to master section
- Fixed slave hashrate graph spikes on master homepage

### 2024-12-26: Remote Configuration
- Added remote slave configuration (frequency, voltage, fan)
- Added bulk actions for all slaves
- Added slave identify (LED flash) feature
- Added restart individual/all slaves

### 2024-12-15: Share Counter Fix
**Problem**: Dashboard showed "0 accepted" even though pool was accepting shares from slaves.

**Root Cause**: When master submitted slave shares to pool, there was no tracking of which stratum message ID corresponded to which slave.

**Fix**:
1. Added `pending_cluster_shares[]` array to track `send_uid` → `slave_id` mapping
2. Modified `stratum_submit_share_from_cluster()` to accept and track `slave_id`
3. Added `cluster_notify_share_result()` callback for stratum_task.c
4. stratum_task.c now calls this callback when pool responds to share
