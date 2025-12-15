# ClusterAxe Development Status & Roadmap

## Project Overview

ClusterAxe is a distributed Bitcoin mining firmware for BitAxe devices. It enables multiple BitAxe units to operate as a coordinated cluster, sharing a single stratum connection and distributing work efficiently.

**Architecture:**
- **Master**: Maintains stratum connection, distributes work to slaves, submits all shares to pool
- **Slave**: Receives work via ESP-NOW, mines with ASIC, reports shares back to master
- **Pool View**: Sees one worker with combined hashrate from all cluster nodes

**Max Cluster Size**: 1 Master + 8 Slaves = 9 BitAxe units

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

### Bug Fixes (This Session)
- [x] **Extranonce2 change detection**: Fixed slave worker_task to detect new work based on extranonce2 changes, not just job_id (master sends same job_id with different extranonce2)
- [x] **Share counter tracking**: Fixed dashboard to show accepted/rejected shares per slave by tracking stratum message IDs and updating counters when pool responds

### Web UI
- [x] Master cluster dashboard showing all connected slaves
- [x] Per-slave stats: hashrate, temperature, fan RPM, shares, frequency, voltage, power
- [x] Slave dashboard showing cluster connection status
- [x] Real-time updates via polling

---

## Known Issues / In Progress

### Needs Testing
- [ ] Share accepted counter fix (just implemented - needs rebuild and verification)

### Potential Issues
- [ ] Thread safety on pending_cluster_shares array (minor race condition possible)
- [ ] Slave timeout detection could be more aggressive
- [ ] No persistence of slave stats across master reboot

---

## Roadmap

### Phase 1: Stability & Polish (Short Term)

#### Branding & Firmware Naming (Priority)
- [ ] **Change AxeOS logo to ClusterAxe** throughout UI
- [ ] **Fix firmware version mismatch issue**
- [ ] **Rename firmware binaries**:
  - Master: `ClusterAxe-Master.bin`
  - Slave: `ClusterAxe-Slave.bin`
  - Update OTA logic to expect new filenames (not `esp-miner.bin`)
- [ ] Update version strings and identifiers throughout codebase

#### Master UI Overhaul (Priority)
- [ ] **Amalgamate Dashboard + Cluster into single "Cluster Dashboard"**
  - Show Master's own hashrate separately
  - Show Cluster total hashrate (master + all slaves combined)
  - Show Cluster total shares (accepted/rejected)
  - Hashrate graph should display cluster total hashrate over time
  - Slave table with individual stats below
- [ ] **Remove/Disable Swarm feature** - not needed for ClusterAxe
- [ ] **Update system changes**:
  - Pull firmware updates from ClusterAxe GitHub repo (not ESP-Miner)
  - Master should be able to check and push OTA updates to slaves
- [ ] Add slave online/offline indicators with color coding
- [ ] Show time since last heartbeat per slave
- [ ] Sortable slave table (by hashrate, temp, shares, etc.)

#### Slave UI Overhaul (Priority)
- [ ] **Amalgamate Dashboard + Cluster into single "Cluster Dashboard"** (same as master)
  - Show device's own hashrate
  - Show hashrate graph for this device
  - Show machine info (temp, fan, frequency, voltage, power)
  - Show connected status to master (hostname, IP)
  - Show assigned slave ID
  - Show shares sent to master (pending/confirmed)
  - Show work received timestamp
- [ ] **Simplify/dumb down the menu**
  - Remove unnecessary menu items for slave operation
  - Keep it minimal: Dashboard, Settings, API
- [ ] **Remove/Disable Pool settings** - master handles pool connection
- [ ] **Remove/Disable WiFi settings after first setup** - only needed once
  - Maybe hide behind "Advanced" or require confirmation to access
- [ ] **Keep API endpoints functional** - allow manual access for debugging/tweaks
- [ ] Remove Swarm feature (same as master)

#### Communication Optimization
- [ ] **Investigate ways to increase ESP-NOW speed/throughput**
  - Reduce message overhead
  - Optimize packet sizes
  - Evaluate broadcast vs unicast performance
  - Consider message batching for shares
  - Profile and reduce latency in message handling
- [ ] Measure and log communication latency for debugging

#### Reliability
- [ ] Improve ESP-NOW channel synchronization after WiFi reconnect
- [ ] Add configurable retry counts and timeouts
- [ ] Better handling of master reboot (slaves should re-register automatically)
- [ ] Watchdog for stuck states

### Phase 2: Enhanced Features (Medium Term)

#### Configuration
- [ ] Web UI to switch between Master/Slave/Disabled modes (currently compile-time)
- [ ] Configurable cluster ID to support multiple clusters on same network
- [ ] Master can remotely configure slave settings (frequency, voltage)

#### Auto-Tuning (Master Controls Slaves)
- [ ] **Master auto-tune slaves for optimal hashrate**
  - Remotely adjust slave frequency and voltage
  - Run tuning algorithm per-slave
  - Find maximum stable hashrate
- [ ] **Master auto-tune slaves for optimal efficiency**
  - Calculate J/TH (Joules per Terahash) per slave
  - Find sweet spot between hashrate and power consumption
  - Support different tuning profiles (max hashrate, max efficiency, balanced)
- [ ] **Cluster-wide thermal management**
  - Monitor all slave temperatures
  - Throttle slaves approaching thermal limits
  - Balance workload based on thermal headroom

#### Monitoring & Stats
- [ ] Historical hashrate graphs per slave
- [ ] Share submission latency tracking
- [ ] ESP-NOW packet loss statistics
- [ ] Power efficiency metrics (J/TH) per slave and cluster total
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
├── cluster_protocol.h     # Protocol message definitions
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
| `CONFIG_CLUSTER_HEARTBEAT_MS` | 5000 | Heartbeat interval |
| `CONFIG_CLUSTER_TIMEOUT_MS` | 15000 | Slave timeout threshold |

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

### 2024-12-15: Share Counter Fix
**Problem**: Dashboard showed "0 accepted" even though pool was accepting shares from slaves.

**Root Cause**: When master submitted slave shares to pool, there was no tracking of which stratum message ID corresponded to which slave. When pool responded, the slave's counter wasn't updated.

**Fix**:
1. Added `pending_cluster_shares[]` array to track `send_uid` → `slave_id` mapping
2. Modified `stratum_submit_share_from_cluster()` to accept and track `slave_id`
3. Added `cluster_notify_share_result()` callback for stratum_task.c
4. stratum_task.c now calls this callback when pool responds to share
5. Callback looks up slave_id and updates `shares_accepted` or `shares_rejected`

**Files Modified**:
- `cluster_integration.c` - Added tracking and callback
- `cluster_integration.h` - Updated function signatures
- `cluster_master.c` - Added `cluster_master_update_slave_share_count()`
- `stratum_task.c` - Added calls to `cluster_notify_share_result()`
