# ClusterAxe Development Notes

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

#### Balanced Mode (Complete Rewrite)
- **Voltage-only tuning** from device defaults
- Uses device's default frequency from config
- Only iterates through voltage steps
- Finds minimum voltage that achieves 90% of expected hashrate
- Conservative: 60°C temperature limit
- Much faster: only tests 7-9 voltage combinations instead of 77+

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
