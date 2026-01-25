# ClusterAxe Development Notes

## v1.5.0 - Current Testing (January 24, 2026)

### Status: IN TESTING

### Changes Made

#### Autotune Removed
- Removed cluster autotune feature entirely due to instability
- Was causing device crashes and unsafe settings states
- Code removed from `cluster_autotune.c`

#### Safety Watchdog (Standalone)
- Runs independently as background FreeRTOS task
- Monitors: chip temp >= 68°C, input voltage <= 4.9V
- Auto-throttles frequency and voltage when limits exceeded
- Works on master and all connected slaves (via HTTP proxy)
- Toggle from cluster page UI
- Shows throttled device count and reasons

#### Hashrate Benchmark Tool (NEW)
- Frontend-driven benchmarking (runs in browser)
- **Two modes available:**
  - **Benchmark** (blue): Finds best efficiency (lowest J/TH)
  - **Overclock** (orange): Finds max hashrate (ignores efficiency)
- Tests voltage/frequency combinations systematically
- Configurable ranges:
  - Voltage: 1000-1400 mV (step: 25)
  - Frequency: 400-999 MHz (step: 25)
- Test durations: 3 min (quick), 5 min (normal), 10 min (thorough)
- Per-test flow:
  1. Apply settings via PATCH /api/system (no restart needed)
  2. Wait for stabilization (15s)
  3. Collect samples every 15s
  4. Analyze with outlier trimming
- Safety checks: max temp, max VR temp, max power
- **Auto-applies best settings when complete**
- 50 test safety limit prevents runaway
- Works on master and slaves with IP addresses

#### Mode Comparison
| Feature | Benchmark | Overclock |
|---------|-----------|-----------|
| Goal | Best J/TH | Max hashrate |
| Max Freq | 625 MHz | 800 MHz |
| Max Voltage | 1300 mV | 1400 mV |
| Max Temp | 66°C | 70°C |
| Max Power | 40W | 50W |
| Selection | Lowest J/TH | Highest GH/s |

#### UI Changes
- Benchmark uses inline expandable panel (not popup dialog)
- Fixed dropdown clipping issues
- Duration selector uses toggle buttons
- Full-width touch-friendly inputs
- Max frequency limit raised to 999 MHz
- Topbar shows hostname (IP in tooltip)

#### Bug Fixes
- Fixed version.txt CRLF causing version mismatch
- Renamed tag from `v1.5.0` to `ClusterAxe-v1.5.0`
- Removed device restart (Bitaxe applies settings immediately)
- Reduced stabilization from 60s to 15s

### Files Modified
- `main/cluster/cluster_autotune.c` - removed autotune code
- `main/cluster/cluster_watchdog.c` - standalone watchdog
- `main/cluster/cluster_watchdog.h` - watchdog header
- `main/http_server/http_server.c` - watchdog endpoints
- `axe-os/src/app/services/benchmark.service.ts` - NEW
- `axe-os/src/app/components/cluster/cluster.component.ts`
- `axe-os/src/app/components/cluster/cluster.component.html`
- `axe-os/src/app/prime-ng.module.ts` - AccordionModule
- `version.txt` - updated to ClusterAxe-v1.5.0 (no CRLF)
- `RELEASE_NOTES.md` - v1.5.0 notes added

### Testing Checklist
- [ ] Version mismatch warning gone
- [ ] Watchdog toggle works
- [ ] Watchdog detects high temp and throttles
- [ ] Watchdog detects low voltage and throttles
- [ ] Benchmark panel opens/closes for master
- [ ] Benchmark panel opens/closes for slaves
- [ ] Benchmark mode starts and finds best efficiency
- [ ] Overclock mode starts and finds max hashrate
- [ ] Benchmark stop button works
- [ ] Results display correctly (mode-specific selection)
- [ ] Best result highlighted and auto-applied
- [ ] Duration toggle buttons work
- [ ] Settings apply without device restart
- [ ] Input fields allow up to 999 MHz

---

## Recent Changes (v1.1.2)

### Dashboard Cluster Summary
- Added average temperature stat (color-coded red at 65°C+)
- Added total cluster efficiency in J/TH
- Grid now has 6 stats: Master, Total Hashrate, Total Shares, Total Power, Avg Temp, Efficiency

### Auto-Timing Feature
- Enabled by default (was disabled)
- UI now visible on Dashboard for all modes (was cluster master only)
- Dynamically adjusts ASIC job intervals (500-800ms) based on share rejection rate

### Cluster Page Fixes
- Master device card now uses `card` class for consistent green glow
- Bulk slave operations now functional:
  - "Set frequency on all" - applies to all active slaves
  - "Set voltage on all" - applies to all active slaves
  - "Restart all" - restarts all active slaves

### Removed Redundant UI
- Removed "Connected Slaves" table from Dashboard (info already at top + dedicated Cluster page)

## Previous Changes (v1.0.3+)

### UI Theme Unification
- Added CRT glow effect to header/topbar in dark mode
- Added CRT glow effect to navigation sidebar in dark mode
- Added text-shadow glow to active/hover menu items
- Changed cluster status sections from `surface-card` to `card` class for consistent glow

### Cluster Page - Input Voltage Display
- Master and slave cards now show both core voltage and input voltage: "Core / Input: 1200 mV / 5.05V"
- Input voltage color-coded: red if < 4.9V, orange if 4.9-5.0V
- **Note**: Master voltage is in millivolts (divide by 1000), slaves report in volts directly

### Watchdog Refinements
- Temperature threshold changed from 65°C to **68°C** for watchdog protection
- When temp > 68°C: now drops **both** frequency and voltage (was only voltage)
- Autotune still uses 65°C as target (more conservative for finding optimal settings)
- Added `TEMP_WATCHDOG_C` constant separate from `TEMP_TARGET_C`

## Bugs Found

### v1.0.3 Tag - version.txt mismatch
- **Issue**: Root `version.txt` contained `ClusterAxe-v1.0.2` but UI had `ClusterAxe-v1.0.3`
- **Result**: UI showed "Firmware and UI versions do not match" warning after flashing
- **Fix**: Updated `version.txt` to `ClusterAxe-v1.0.3`
- **Note**: This should be fixed in the repo for future releases

## Auto-Tuner Improvements (IMPLEMENTED)

### Changes Made to `cluster_autotune.c`

#### Extended Frequency/Voltage Ranges
- Frequency steps: 450-900 MHz (added 850, 900 for hashrate mode)
- Voltage steps: 1100-1350 mV (added 1325, 1350 for hashrate mode)

#### Mode-Specific Limits
| Mode | Max Freq | Max Voltage | Temp Limit |
|------|----------|-------------|------------|
| Efficiency | 625 MHz | 1200 mV | 65°C |
| Balanced | 700 MHz | 1250 mV | 60°C |
| Hashrate | 900 MHz | 1350 mV | 68°C |

#### New Hashrate Validation
- Expected hashrate calculation: `freq × small_core_count × asic_count / 1000`
- Hashrate ratio tracking: `actual / expected` (threshold: 90%)
- Hashrate stability tracking via coefficient of variation
- Logs show actual vs expected percentage for each test

#### High Hashrate Mode
- Pushes limits: up to 900 MHz, 1350 mV
- Higher temp tolerance: 68°C (same as watchdog)
- Requires at least 75% of expected hashrate
- Optimizes for maximum actual hashrate

#### Efficiency Mode
- Best J/TH while maintaining ≥90% of expected hashrate
- Validates hashrate ratio before accepting a result
- Warns when hashrate underperforms (may need more voltage)
- Temperature limit: 65°C

#### Balanced Mode (Conservative Tuning)
- Tests frequencies: **500, 550, 600, 650, 700 MHz**
- Tests voltage steps at each frequency
- Conservative: 60°C temperature limit
- Finds best efficiency while achieving ≥90% of expected hashrate
- Total tests: 5 frequencies × 7 voltages = ~35 combinations

### Key Metrics Tracked
1. **Expected hashrate**: freq × cores × asic_count / 1000
2. **Actual hashrate**: measured from ASIC
3. **Hashrate ratio**: actual / expected
4. **Efficiency**: J/TH = (power × 1000) / hashrate_GH
5. **Stability**: coefficient of variation (std_dev / mean)
6. **Min/Max hashrate** during test period

### Helper Functions Added
- `calculate_expected_hashrate(freq_mhz)` - calculates theoretical hashrate
- `get_device_default_frequency()` - gets ASIC default freq from config
- `get_device_default_voltage()` - gets ASIC default voltage from config
- `get_temp_limit_for_mode(mode)` - returns mode-specific temp limit
- `calculate_hashrate_ratio(actual, expected)` - computes ratio
- `calculate_hashrate_stability()` - computes coefficient of variation

## Dual Pool Cluster Mode (v1.1.x) - NOT PRODUCTION READY

### Issues identified during testing:
1. **Race condition in slave share submission**: Slave reads `pool_id` from `current_work` which may have changed by the time share is found
2. **Attempted fix made things worse**: Adding per-job pool_id tracking led to 47.7% rejection rate and negative share counters
3. **Cross-chain pools**: Testing with BTC primary + BCH secondary added complexity

### Recommendations for future work:
- Test with same-coin pools first before cross-chain
- Per-job pool_id tracking needs proper implementation
- Share result routing and counter tracking needs redesign
