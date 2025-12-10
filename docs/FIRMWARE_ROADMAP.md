# Clusteraxe Firmware Development Roadmap

## Project Overview

This document outlines the development roadmap for creating Master and Slave firmware variants for the Bitaxe Cluster (Clusteraxe) project. The goal is to enable multiple Bitaxe devices to operate as a coordinated mining cluster using the BAP (Bitaxe Accessory Protocol) for inter-device communication.

**Base Firmware**: ESP-Miner 2.12.0 DualPool (`C:\Users\ShaeOJ\Documents\GitHub\ESP-Miner-2.12.0-DualPool`)
**Cluster Protocol**: BAP-based cluster protocol (already designed in `bitaxe-cluster/`)
**Target**: Two firmware builds - Master and Slave

---

## Architecture Summary

```
                    ┌─────────────────────────┐
                    │      Mining Pool        │
                    │   (Stratum Protocol)    │
                    └───────────┬─────────────┘
                                │
                                │ WiFi/Internet
                                │
                    ┌───────────▼─────────────┐
                    │    MASTER BITAXE        │
                    │  ┌───────────────────┐  │
                    │  │ Stratum Client    │  │
                    │  │ Work Distributor  │  │
                    │  │ Share Aggregator  │  │
                    │  │ Local ASIC Mining │  │
                    │  │ Cluster Manager   │  │
                    │  └───────────────────┘  │
                    └───────────┬─────────────┘
                                │
                    BAP/UART (Custom Wiring)
                                │
        ┌───────────────────────┼───────────────────────┐
        │                       │                       │
┌───────▼───────┐       ┌───────▼───────┐       ┌───────▼───────┐
│  SLAVE #1     │       │  SLAVE #2     │       │  SLAVE #N     │
│ ┌───────────┐ │       │ ┌───────────┐ │       │ ┌───────────┐ │
│ │Work Recv  │ │       │ │Work Recv  │ │       │ │Work Recv  │ │
│ │ASIC Mine  │ │       │ │ASIC Mine  │ │       │ │ASIC Mine  │ │
│ │Share Send │ │       │ │Share Send │ │       │ │Share Send │ │
│ └───────────┘ │       │ └───────────┘ │       │ └───────────┘ │
└───────────────┘       └───────────────┘       └───────────────┘
```

---

## Phase 1: Project Setup & Build Infrastructure [COMPLETED]

### 1.1 Fork and Configure Build System [COMPLETED]

**Tasks:**
- [x] Create Clusteraxe firmware directory structure
- [x] Set up two separate build configurations:
  - `clusteraxe-master` - Master firmware build
  - `clusteraxe-slave` - Slave firmware build
- [x] Add compile-time flag `CONFIG_CLUSTER_MODE` (MASTER/SLAVE)
- [x] Create CMake configuration for conditional compilation
- [x] Set up version numbering scheme (e.g., `Clusteraxe-1.0.0-master`)

**Files Created/Modified:**
```
ESP-Miner-2.12.0-DualPool/
├── CMakeLists.txt              # Updated - added cluster source files
├── sdkconfig.master            # Created - Master build config
├── sdkconfig.slave             # Created - Slave build config
└── main/
    └── cluster/
        ├── cluster_config.h    # Created - compile-time options
        └── Kconfig.projbuild   # Created - menuconfig integration
```

### 1.2 Integrate Existing Cluster Module [COMPLETED]

**Tasks:**
- [x] Copy cluster protocol files from `bitaxe-cluster/main/cluster/` into ESP-Miner source tree
- [x] Add cluster source files to CMakeLists.txt
- [x] Add `cluster` to INCLUDE_DIRS
- [x] Adapt cluster module for ESP-Miner codebase integration

**Files Integrated:**
```
main/cluster/
├── cluster.h           # Public API (adapted)
├── cluster.c           # Core module (adapted)
├── cluster_master.c    # Master logic (adapted)
├── cluster_slave.c     # Slave logic (adapted)
├── cluster_protocol.h  # Protocol definitions
├── cluster_protocol.c  # Protocol encoding/decoding
├── cluster_config.h    # Compile-time configuration
└── Kconfig.projbuild   # ESP-IDF menuconfig integration
```

---

## Phase 2: BAP Integration for Cluster Communication [COMPLETED]

### 2.1 Extend BAP Protocol Handler [COMPLETED]

**Current State:** BAP handles `$BAP,` messages for monitoring/control
**Required:** Route `$CL` messages to cluster module

**Tasks:**
- [x] Modify `bap_handlers.c` to detect cluster messages (prefix `$CL`)
- [x] Add cluster message routing in `BAP_parse_message()`
- [x] Implement cluster-specific message handlers
- [x] Add cluster.h/cluster_config.h includes

**Code Changes Made:**
```c
// In bap_handlers.c - BAP_parse_message()
// Check for cluster messages ($CL...) and route to cluster handler
#if CLUSTER_ENABLED
    if (cluster_is_cluster_message(message)) {
        cluster_on_bap_message_received(message);
        return;
    }
#endif
```

### 2.2 UART Configuration for Multi-Device [COMPLETED - Uses Existing BAP UART]

**Status:** The existing BAP UART infrastructure is already configured correctly:
- UART2 at 115200 baud
- GPIO pins configurable via Kconfig
- Receive buffer and task already implemented in `bap_uart.c`

**Pin Configuration (BAP Header):**
```
Pin 1: 5V (optional power)
Pin 2: TX (Master) / RX (Slave)
Pin 3: RX (Master) / TX (Slave)
Pin 4: GND
```

---

## Phase 3: Master Firmware Development [COMPLETED]

### 3.1 Work Distribution System [COMPLETED]

**Tasks:**
- [x] Hook into `stratum_task.c` to intercept incoming work
- [x] Implement nonce range partitioning algorithm
- [x] Create extranonce2 generator for each slave
- [x] Build work distribution queue for each registered slave
- [x] Implement `$CLWRK` message encoding and transmission

**Integration Points (Implemented):**
```c
// In stratum_task.c - After receiving mining.notify (around line 569)
#if CLUSTER_ENABLED && CLUSTER_IS_MASTER
                cluster_master_on_mining_notify(GLOBAL_STATE,
                                                 stratum_api_v1_message.mining_notification,
                                                 GLOBAL_STATE->extranonce_str,
                                                 GLOBAL_STATE->extranonce_2_len);
#endif
```

**Files Modified:**
- `main/tasks/stratum_task.c` - Added cluster work distribution hook
- `main/tasks/create_jobs_task.c` - Master uses assigned nonce range (slot 0)
- `main/cluster/cluster_integration.c` - Work conversion and distribution
- `main/cluster/cluster_master.c` - Nonce range calculation and work sending

**Master Nonce Range (in create_jobs_task.c):**
```c
#if CLUSTER_ENABLED && CLUSTER_IS_MASTER
    // In cluster master mode, limit nonce range to master's assigned slot
    if (cluster_is_active()) {
        uint32_t nonce_start, nonce_end;
        cluster_master_get_local_nonce_range(&nonce_start, &nonce_end);
        queued_next_job->starting_nonce = nonce_start;
    }
#endif
```

### 3.2 Slave Registration & Management [COMPLETED]

**Tasks:**
- [x] Implement `$CLREG` message handler
- [x] Create slave state tracking structure (up to 8 slaves)
- [x] Assign unique slave IDs (1-8)
- [x] Send `$CLACK` acknowledgment with assigned ID
- [x] Implement timeout detection for disconnected slaves
- [x] Recalculate nonce ranges when slaves join/leave

**Slave States (Implemented in cluster.h):**
```c
typedef enum {
    SLAVE_STATE_DISCONNECTED = 0,
    SLAVE_STATE_REGISTERING,
    SLAVE_STATE_ACTIVE,
    SLAVE_STATE_STALE
} slave_state_t;
```

### 3.3 Share Aggregation & Submission [COMPLETED]

**Tasks:**
- [x] Implement `$CLSHR` message handler (receive shares from slaves)
- [x] Validate received shares against distributed work
- [x] Submit valid shares to pool using master's stratum connection
- [x] Track per-slave share statistics
- [x] Implement share rejection feedback to slaves

**Flow (Implemented):**
```
Slave finds share → $CLSHR to Master → Master validates
    → Master submits via stratum_submit_share_from_cluster()
    → Pool accepts/rejects → Update slave statistics
```

### 3.4 Heartbeat & Health Monitoring [COMPLETED]

**Tasks:**
- [x] Implement heartbeat receive handler (`$CLHBT`)
- [x] Track last heartbeat timestamp per slave
- [x] Detect slave timeout (10 second threshold)
- [x] Mark stale slaves and redistribute their nonce range
- [x] Send cluster status updates to web interface via `/api/cluster/status`

**Files Created/Modified:**
- `main/cluster/cluster_master.c` - Coordinator task, share submitter task
- `main/http_server/http_server.c` - Added `/api/cluster/status` endpoint

---

## Phase 4: Slave Firmware Development [IN PROGRESS]

### 4.1 Disable Pool Connectivity [COMPLETED]

**Tasks:**
- [x] Conditionally skip stratum work in create_jobs_task.c for slave builds
- [x] Intercept share submission in asic_result_task.c for slave mode
- [x] Keep WiFi for configuration/monitoring (optional, AP mode)
- [x] Cluster init in main.c with conditional compilation

**Implementation (in create_jobs_task.c):**
```c
#if CLUSTER_ENABLED && CLUSTER_IS_SLAVE
        // In slave mode, work comes from cluster master, not stratum
        if (cluster_slave_should_skip_stratum()) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WORK_WAIT_TIMEOUT_MS));
            continue;
        }
#endif
```

### 4.2 Work Reception System [COMPLETED]

**Tasks:**
- [x] Implement `$CLWRK` message handler
- [x] Parse work fields (job_id, prevhash, merkle, version, nbits, ntime)
- [x] Parse nonce range assignment (nonce_start, nonce_end)
- [x] Parse extranonce2 assignment
- [x] Convert cluster work to internal `bm_job` format
- [x] Submit work to ASIC task queue via cluster_slave_submit_to_asic()

**Files Modified:**
- `main/tasks/create_jobs_task.c` - Skip stratum in slave mode
- `main/tasks/asic_result_task.c` - Route shares to master in slave mode
- `main/cluster/cluster_integration.c` - cluster_slave_submit_to_asic()
- `main/cluster/cluster_slave.c` - Work reception and ASIC submission

**Integration:**
```c
// Replace stratum work queue with cluster work queue
void cluster_work_received(cluster_work_t *work) {
    bm_job job;
    convert_cluster_work_to_job(work, &job);
    job.nonce_start = work->nonce_start;
    job.nonce_end = work->nonce_end;
    asic_queue_job(&job);
}
```

### 4.3 Share Submission to Master [COMPLETED]

**Tasks:**
- [x] Hook into `asic_result_task.c` to intercept found shares
- [x] Encode share as `$CLSHR` message
- [x] Include: slave_id, job_id, nonce, ntime, version, extranonce2
- [x] Send share to master via BAP/UART
- [x] Track local share statistics (found, sent)

**Implementation (in asic_result_task.c):**
```c
#if CLUSTER_ENABLED && CLUSTER_IS_SLAVE
            if (cluster_slave_should_skip_stratum()) {
                cluster_slave_intercept_share(GLOBAL_STATE, job_id, nonce, ntime, version, extranonce2);
            } else
#endif
            { /* normal pool submission */ }
```

**Files Modified:**
- `main/tasks/asic_result_task.c` - Share interception for slave mode
- `main/cluster/cluster_slave.c` - `share_sender_task`, `cluster_slave_submit_share()`
- `main/cluster/cluster_protocol.c` - `cluster_protocol_encode_share()`

### 4.4 Registration & Heartbeat [COMPLETED]

**Tasks:**
- [x] On boot, send `$CLREG` message with hostname
- [x] Wait for `$CLACK` response with assigned slave_id
- [x] Store assigned slave_id in runtime state
- [x] Implement periodic heartbeat task (every 3 seconds)
- [x] Include in heartbeat: hashrate, temperature, fan RPM, share count
- [x] Handle master timeout/reconnection

**Implementation (in cluster_slave.c):**
```c
static void heartbeat_task(void *pvParameters)
{
    // Initial registration
    cluster_slave_register(cluster_get_hostname());

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(CLUSTER_HEARTBEAT_MS));
        if (g_slave->registered) {
            send_heartbeat();
        } else {
            cluster_slave_register(hostname);  // Retry
        }
    }
}
```

---

## Phase 5: Web Interface & Monitoring [COMPLETED]

### 5.1 Master Dashboard Extensions [COMPLETED]

**Tasks:**
- [x] Add `/api/cluster/status` endpoint
- [x] Return JSON with cluster state:
  - Mode (master/slave/disabled)
  - Number of active slaves
  - Per-slave statistics (hashrate, shares, temp)
  - Total cluster hashrate
  - Work distributed count
- [x] Slave list embedded in status response
- [ ] Add cluster configuration to settings page (future)

**Files Modified:**
- `main/http_server/http_server.c` - `GET_cluster_status()` handler

**API Response (Master):**
```json
{
  "enabled": true,
  "mode": 1,
  "modeString": "master",
  "activeSlaves": 2,
  "totalHashrate": 80000,
  "totalShares": 142,
  "totalSharesAccepted": 140,
  "totalSharesRejected": 2,
  "slaves": [
    {
      "slot": 0,
      "slaveId": 0,
      "hostname": "bitaxe-001",
      "state": 2,
      "hashrate": 40000,
      "temperature": 52.3,
      "fanRpm": 4500,
      "sharesSubmitted": 71,
      "sharesAccepted": 70,
      "lastSeen": 1702000000
    }
  ]
}
```

### 5.2 Slave Minimal Interface [COMPLETED]

**Tasks:**
- [x] Create simplified web interface for slaves
- [x] Show connection status to master
- [x] Display local mining statistics
- [x] Show assigned nonce range
- [ ] Allow hostname configuration (future)
- [x] Enable/disable cluster mode toggle

**Files Created:**
- `main/http_server/axe-os/src/app/components/cluster/cluster.component.ts`
- `main/http_server/axe-os/src/app/components/cluster/cluster.component.html`
- `main/http_server/axe-os/src/app/components/cluster/cluster.component.scss`
- `main/http_server/axe-os/src/app/services/cluster.service.ts`

**Files Modified:**
- `main/http_server/axe-os/src/app/app.module.ts` - Added ClusterComponent
- `main/http_server/axe-os/src/app/app-routing.module.ts` - Added /cluster route
- `main/http_server/axe-os/src/app/layout/app.menu.component.ts` - Added Cluster menu item

### 5.3 AxeOS Integration (Future)

**Tasks:**
- [ ] Design cluster view for AxeOS Live! dashboard
- [ ] Aggregate statistics from multiple masters
- [ ] Real-time cluster health monitoring
- [ ] Historical performance graphs per slave

---

## Phase 6: Configuration & Persistence [COMPLETED]

### 6.1 NVS Configuration Keys [COMPLETED]

**Tasks:**
- [x] Add `cluster_mode` NVS key (0=disabled, 1=master, 2=slave)
- [x] Add `cluster_max_slaves` configuration (via Kconfig)
- [x] Add `cluster_heartbeat_interval` configuration (via Kconfig)
- [x] Add `cluster_timeout` configuration (via Kconfig)
- [x] Implement configuration save/load functions

**Implementation (in cluster.c):**
```c
#define NVS_NAMESPACE "cluster"
#define NVS_KEY_MODE  "mode"

static cluster_mode_t load_mode_from_nvs(void);
static esp_err_t save_mode_to_nvs(cluster_mode_t mode);
```

### 6.2 Configuration Web API [COMPLETED]

**Tasks:**
- [x] Add `POST /api/cluster/mode` endpoint for mode changes
- [x] Validate cluster configuration changes
- [x] Implement mode switch (requires reboot)
- [x] Save settings to NVS on change

**Files Modified:**
- `main/http_server/http_server.c` - Added `POST_cluster_mode()` handler

---

## Phase 7: Testing & Validation [IN PROGRESS]

**Build Status:**
- [x] Master firmware built successfully
- [x] Slave firmware built successfully
- [x] Web UI compiled and included in www.bin

### 7.1 Unit Testing

**Tasks:**
- [ ] Test protocol encoding/decoding functions
- [ ] Test nonce range calculation accuracy
- [ ] Test message checksum validation
- [ ] Test work conversion functions

### 7.2 Integration Testing [READY FOR HARDWARE]

**Tasks:**
- [ ] Test master-slave registration flow
- [ ] Test work distribution and reception
- [ ] Test share submission and aggregation
- [ ] Test heartbeat timeout detection
- [ ] Test slave reconnection after disconnect

### 7.3 Performance Testing

**Tasks:**
- [ ] Measure UART message latency
- [ ] Verify no duplicate nonce ranges
- [ ] Test with maximum slaves (8)
- [ ] Measure cluster hashrate vs individual hashrates
- [ ] Monitor memory usage on master with many slaves

### 7.4 Stress Testing

**Tasks:**
- [ ] Run 24-hour continuous mining test
- [ ] Test rapid slave connect/disconnect cycles
- [ ] Test behavior with unreliable UART connection
- [ ] Verify no memory leaks over extended operation

---

## Phase 8: Documentation & Release [MOSTLY COMPLETE]

### 8.1 User Documentation [COMPLETED]

**Tasks:**
- [x] Write master setup guide
- [x] Write slave setup guide
- [x] Document wiring diagrams for BAP cable
- [x] Create troubleshooting guide
- [x] Document LED indicators for cluster status

**Files Created:**
- `USER_GUIDE.md` - Complete setup and usage guide
- `BAP_WIRING.md` - Quick reference wiring diagrams
- `INVESTMENT_PROPOSAL.md` - Project investment documentation

### 8.2 Developer Documentation

**Tasks:**
- [x] Document cluster protocol specification (in FIRMWARE_ROADMAP.md)
- [x] Document API endpoints (in USER_GUIDE.md)
- [ ] Create code architecture overview
- [ ] Write contribution guidelines

### 8.3 Release Package [PENDING TESTING]

**Tasks:**
- [x] Create pre-built firmware binaries
  - `clusteraxe-master.bin` (built)
  - `clusteraxe-slave.bin` (built)
- [x] Package with flashing instructions (in USER_GUIDE.md)
- [ ] Create GitHub release with changelog
- [ ] Submit to Bitaxe community channels

---

## Hardware Considerations (Your Responsibility)

### Custom BAP Cable Design
```
Master BAP Header          Slave BAP Header
┌────────────────┐        ┌────────────────┐
│ Pin 1: 5V      │───────│ Pin 1: 5V      │ (Optional power)
│ Pin 2: TX      │───────│ Pin 3: RX      │ (Cross-connect)
│ Pin 3: RX      │───────│ Pin 2: TX      │ (Cross-connect)
│ Pin 4: GND     │───────│ Pin 4: GND     │ (Common ground)
└────────────────┘        └────────────────┘
```

### Multi-Slave Wiring Options
1. **Daisy Chain**: Master → Slave1 → Slave2 → ... (requires signal buffering)
2. **Star Topology**: Master → Hub → [Slave1, Slave2, ...] (recommended)
3. **RS-485 Bus**: Master → Bus → [All Slaves] (future enhancement)

### Enclosure Design Considerations
- Adequate ventilation for combined heat output
- Cable management for BAP connections
- Status LED visibility
- Power distribution (shared PSU vs individual)

---

## Implementation Priority

### Minimum Viable Product (MVP)
1. Phase 1: Build infrastructure
2. Phase 2: BAP integration
3. Phase 3.1-3.3: Basic master functionality
4. Phase 4.1-4.3: Basic slave functionality
5. Phase 7.2: Integration testing

### Full Feature Set
- Add all Phase 3 & 4 features
- Complete Phase 5 (web interface)
- Complete Phase 6 (configuration)
- Full Phase 7 testing

### Future Enhancements
- RS-485 support for larger clusters
- Automatic master election
- Stratum V2 support
- AxeOS Live! integration

---

## Phase 7: Hybrid Control System (Option C) [COMPLETED]

This phase implements the "Hybrid Control" architecture for managing cluster devices:

### 7.1 Extended Protocol for Device Discovery [COMPLETED]

**Tasks:**
- [x] Add IP address to slave registration message (`$CLREG,hostname,ip_addr`)
- [x] Add extended stats to heartbeat (`$CLHBT,...,freq,voltage,power,vin`)
- [x] Implement extended encode/decode functions
- [x] Maintain backwards compatibility with legacy messages

**Protocol Extensions:**
```
Extended Registration: $CLREG,hostname,ip_addr*XX
Extended Heartbeat:    $CLHBT,slave_id,hashrate,temp,fan,shares,freq,voltage,power,vin*XX
```

**Files Modified:**
- `main/cluster/cluster_protocol.h` - Added `cluster_heartbeat_data_t` struct
- `main/cluster/cluster_protocol.c` - Added `_ex` encode/decode functions
- `main/cluster/cluster.h` - Extended `cluster_slave_t` with new fields
- `main/cluster/cluster.c` - Use extended decode in message routing

### 7.2 Master: Extended Slave Tracking [COMPLETED]

**Tasks:**
- [x] Store slave IP address from registration
- [x] Update slave info with extended heartbeat data (frequency, voltage, power)
- [x] Expose all slave fields via `/api/cluster/status` endpoint
- [x] Track per-slave power consumption

**Slave Data Structure:**
```c
typedef struct {
    char hostname[32];
    char ip_addr[16];       // NEW: Slave IP for web access
    uint16_t frequency;     // NEW: ASIC frequency (MHz)
    uint16_t core_voltage;  // NEW: Core voltage (mV)
    float power;            // NEW: Power consumption (W)
    float voltage_in;       // NEW: Input voltage (V)
    // ... existing fields
} cluster_slave_t;
```

**Files Modified:**
- `main/cluster/cluster_master.c` - `handle_registration()` stores IP, `handle_heartbeat_ex()` stores extended stats
- `main/http_server/http_server.c` - Added `ipAddr`, `frequency`, `coreVoltage`, `power`, `voltageIn` to slave JSON

### 7.3 Slave: Extended Stats Reporting [COMPLETED]

**Tasks:**
- [x] Add weak functions for gathering extended stats
- [x] Include IP address in registration message
- [x] Send extended heartbeat with frequency, voltage, power data
- [x] Link weak functions to actual ESP-Miner system getters

**Strong Implementations (in cluster_integration.c):**
```c
// These override the weak defaults in cluster_slave.c
const char* cluster_get_ip_addr(void)      // → SYSTEM_MODULE.ip_addr_str
uint16_t cluster_get_asic_frequency(void)  // → POWER_MANAGEMENT_MODULE.frequency_value
uint16_t cluster_get_core_voltage(void)    // → POWER_MANAGEMENT_MODULE.voltage * 1000
float cluster_get_power(void)              // → POWER_MANAGEMENT_MODULE.power
float cluster_get_voltage_in(void)         // → Power_get_input_voltage() / 1000
uint32_t cluster_get_asic_hashrate(void)   // → SYSTEM_MODULE.current_hashrate * 100 (fixed)
float cluster_get_chip_temp(void)          // → POWER_MANAGEMENT_MODULE.chip_temp_avg
uint16_t cluster_get_fan_rpm(void)         // → POWER_MANAGEMENT_MODULE.fan_rpm
const char* cluster_get_hostname(void)     // → NVS hostname or "bitaxe"
```

**Files Modified:**
- `main/cluster/cluster_slave.c` - Extended `cluster_slave_register()` and `send_heartbeat()`
- `main/cluster/cluster_integration.c` - Added strong implementations of all getter functions
- `main/cluster/cluster_integration.h` - Added function declarations

### 7.4 Web UI: Clickable Slave Links [COMPLETED]

**Tasks:**
- [x] Display slave IP as clickable link in master dashboard
- [x] Open slave web interface in new tab when clicked
- [x] Show extended stats (frequency, voltage, power) in slave table
- [x] Add total cluster power summary
- [x] Add API access information panel

**UI Features:**
- **Device column**: Shows hostname and clickable IP link
- **Extended columns**: Freq (MHz), Voltage (mV), Power (W)
- **Power summary**: Total cluster power consumption
- **API info**: Quick reference for master and slave API endpoints

**Files Modified:**
- `main/http_server/axe-os/src/app/services/cluster.service.ts` - Extended `IClusterSlave` interface
- `main/http_server/axe-os/src/app/components/cluster/cluster.component.html` - Enhanced table, added power summary
- `main/http_server/axe-os/src/app/components/cluster/cluster.component.ts` - Added `getTotalPower()` method

### 7.5 External API Access [COMPLETED]

The Hybrid architecture provides full API access to both master and slave devices:

**Master API Endpoints:**
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/cluster/status` | GET | Full cluster status including all slave data |
| `/api/cluster/mode` | POST | Change cluster mode (requires restart) |
| `/api/system/info` | GET | Master device information |

**Slave API Endpoints (accessible via slave IP):**
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/system/info` | GET | Slave device information |
| `/api/swarm/info` | GET | Mining statistics |
| `/api/system/settings` | POST | Configure slave settings |
| `/api/system/restart` | POST | Restart slave device |

**Example: Query all devices from external app:**
```bash
# Get master cluster status
curl http://master-ip/api/cluster/status

# Get individual slave info
curl http://192.168.1.101/api/system/info

# Change slave frequency (example)
curl -X POST http://192.168.1.101/api/system/settings \
     -H "Content-Type: application/json" \
     -d '{"frequency": 575}'
```

---

## Estimated Effort Breakdown

| Phase | Description | Complexity |
|-------|-------------|------------|
| 1 | Project Setup | Low |
| 2 | BAP Integration | Medium |
| 3 | Master Firmware | High |
| 4 | Slave Firmware | High |
| 5 | Web Interface | Medium |
| 6 | Configuration | Low |
| 7 | Testing | Medium |
| 8 | Documentation | Low |

---

## Getting Started

### Prerequisites
- ESP-IDF v5.5.1 installed
- Node.js v22+ (for web UI)
- Python 3.4+ (build tools)
- esptool v4.9.0 or earlier
- Two or more Bitaxe devices for testing

### First Steps
1. Clone ESP-Miner-2.12.0-DualPool repository
2. Copy cluster module files from `bitaxe-cluster/`
3. Set up dual build configuration
4. Compile and flash master firmware to one device
5. Compile and flash slave firmware to second device
6. Wire BAP connection between devices
7. Begin testing registration flow

---

## Notes & Considerations

### Why DualPool Base Firmware?
The DualPool firmware already has:
- Robust dual-queue work management
- Per-destination statistics tracking
- Load balancing algorithm (dithering)
- Clean task separation
- Modern codebase (ESP-IDF 5.5.1)

These patterns map well to cluster functionality where the master distributes to multiple "destinations" (slaves instead of pools).

### Memory Constraints
- ESP32-S3 has 8MB PSRAM
- Each slave state: ~200 bytes
- Work queue per slave: ~4KB
- Total cluster overhead: ~50KB for 8 slaves
- Leaves plenty of headroom

### Protocol Design Rationale
- NMEA-style text protocol: Human readable, easy to debug
- Hex encoding for binary data: Avoids byte-stuffing complexity
- XOR checksum: Simple but effective for UART errors
- 3-second heartbeat: Balances responsiveness with bandwidth

---

## Contact & Collaboration

This roadmap is a living document. As development progresses, we'll update phases, add discovered requirements, and refine estimates.

**Repository**: [Clusteraxe Project]
**License**: GPL-3.0 (same as ESP-Miner)
