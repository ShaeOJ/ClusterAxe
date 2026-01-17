# ClusterAxe Release Notes

## v1.1.2 (January 16, 2026)

### New Features

- **Enhanced Auto-Tuner Modes**: Improved auto-tuner with mode-specific temperature limits and extended ranges:
  - **Efficiency Mode**: 625 MHz max, 1200 mV max, 65°C temp limit with hashrate validation (90% threshold)
  - **Hashrate Mode**: 900 MHz max, 1350 mV max, 68°C temp limit - push limits for maximum performance
  - **Balanced Mode**: Tests frequencies 500, 550, 600, 650, 700 MHz with voltage tuning at each, 60°C temp limit

- **Hashrate Validation**: Auto-tuner now validates actual vs expected hashrate ratio to ensure ASICs achieve proper performance at each setting

- **Slave Hashrate Registers on Cluster Page**: Expanded slave panels now show hashrate domain registers with heatmap coloring

- **Slave Hashrate Registers on Master Dashboard**: Master dashboard can now display hashrate registers from all connected slaves alongside its own registers (click refresh to fetch)

- **Best Found Hashrate**: Auto-tuner "Best Found" card now displays the hashrate achieved at the best settings

### Bug Fixes

- **Slave Fan Speed Setting**: Fixed fan speed not applying to slaves - was using read-only `fanspeed` field instead of `manualFanSpeed`

- **Slave Target Temp Setting**: Fixed target temperature not applying to slaves - was using wrong field name `autofantemp` instead of `temptarget`

- **Master Fan Speed Setting**: Fixed master fan speed setting on cluster page using wrong field

- **Oscilloscope Animation**: Fixed black space appearing at bottom of oscilloscope when auto-tuner animation starts (SVG baseline gap issue)

### UI Improvements

- Updated auto-tune mode descriptions to match actual backend frequency/voltage/temperature limits

---

## v1.1.1 (January 11, 2026)

### Bug Fixes

- **Pool-Aware Merkle Root Computation**: Fixed critical bug where dual pool mode used wrong pool's coinbase/extranonce data when computing merkle roots for slaves, causing high rejection rates.
  - Notify data is now stored per-pool (separate storage for primary and secondary)
  - Merkle roots are computed using the correct pool's data based on work's pool_id

- **Work Rebroadcast Timing Sync**: Work rebroadcast interval now syncs with auto-timing instead of being hardcoded at 700ms.
  - Ensures work distribution aligns with ASIC mining cycle
  - Reduces stale share rejections when auto-timing adjusts interval

- **Job Mapping Pool Collision**: Job mapping lookup now includes pool_id to prevent collisions when both pools send jobs with the same hash.
  - First tries exact match (numeric_id + pool_id)
  - Falls back to numeric_id only for backwards compatibility

- **Pool Balance Applied to Cluster Distribution**: Cluster work distribution now respects the pool balance setting.
  - Previously all work from both pools was distributed regardless of balance
  - Now tracks distribution counts per pool and maintains configured ratio
  - Actual share distribution should match pool balance setting

---

## v1.1.0 (January 11, 2026)

### New Features

- **Dual Pool Cluster Support**: Cluster master now distributes work from BOTH pools to slaves in dual pool mode. Previously only primary pool work was distributed.
  - Slaves receive work tagged with pool_id (0=primary, 1=secondary)
  - Slave shares are routed back to the correct pool based on which pool's work they mined
  - Pool difficulty is synced per-pool to slaves
  - Backwards compatible: Old slaves still work but only mine primary pool

- **Per-Pool Share Tracking**: Track and display shares separately for each pool in dual pool cluster mode.
  - Cluster page shows per-pool share counts when dual pool is active
  - Master dashboard displays "Dual Pool Distribution" section with:
    - Per-pool share counts (accepted/rejected)
    - Estimated hashrate per pool (based on share ratio)
    - Visual split bar showing pool distribution percentage
  - API endpoint `/api/cluster/status` now returns per-pool stats

### Protocol Changes

- Added `pool_id` field to cluster work messages (CLWRK)
- Added `pool_id` field to cluster share messages (CLSHR)
- Protocol remains backwards compatible (pool_id defaults to 0 if missing)

---

## v1.0.3 (January 10, 2026)

### Improvements

- **Cleaner Console Output**: Removed excessive debug logging from cluster code. Important messages still logged, but verbose debug output reduced significantly.

- **Improved Slave Share Display**: Slave UI now shows shares found vs submitted separately, with pending/failed count if applicable.

- **Master Pool Difficulty**: Slaves now display the master's pool difficulty in the Master Connection card.

- **Shares Per Hour**: Added shares per hour calculation to slave performance stats.

### Bug Fixes

- **HTTP Server Handler Limit**: Increased max URI handlers from 30 to 40 to support all API endpoints.

---

## v1.0.2 (December 31, 2025)

### Bug Fixes

- **Slave Settings Now Apply**: Fixed issue where manually changing frequency/voltage on slaves via the cluster UI did nothing. Settings now correctly apply via HTTP PATCH to slave devices.

- **Slave Device Info Loads**: Fixed slave dropdown showing "Unknown" firmware, 0s uptime, etc. Now correctly fetches device info from slave's `/api/system/info` endpoint.

- **Master Settings Input Stability**: Fixed master settings dropdown resetting input values every 3 seconds during polling. Values now persist while editing.

- **Watchdog Always Active**: Safety watchdog now continuously monitors temperature and input voltage, even during autotune. If limits are exceeded (temp > 65°C or Vin < 4.9V), autotune is stopped and settings are reduced.

### Improvements

- HTTP proxy now handles both chunked and non-chunked responses from slaves
- Added 60-second cooldown after watchdog triggers before allowing settings to increase again

---

## v1.0.1 (December 30, 2025)

### New Features

- **Cluster-Wide Autotune**: Master can now autotune all slaves sequentially via HTTP
- **Safety Watchdog**: Background monitoring with automatic voltage/frequency reduction
- **Device Selection**: Choose which devices to include in cluster autotune (master, specific slaves, or all)
- **Slave IP Clickable**: Slave IP addresses are now clickable links to their web UI

### UI Improvements

- Redesigned Cluster Auto-Tune section with 3-column layout
- Oscilloscope visualization during autotune
- Real-time progress display showing current device, frequency, voltage, and test count
- Best results panel with efficiency metrics
- Watchdog toggle with shield icon in header

### Bug Fixes

- Fixed master hashrate display (was showing 1/100th of actual value)
- Fixed watchdog toggle sending wrong state
- Fixed button styling on danger buttons (now solid red with white text)

---

## v1.0.0 (December 28, 2025)

### Initial Release

- ESP-NOW wireless cluster communication
- Master/Slave mode selection
- Real-time hashrate, temperature, and power monitoring
- Per-slave configuration panels
- Cluster statistics dashboard
- Autotune with efficiency/balanced/hashrate modes
- Profile save/load system
- Share rejection explanations
- Slave mode UI with cluster connection status
