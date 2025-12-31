# ClusterAxe Release Notes

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
