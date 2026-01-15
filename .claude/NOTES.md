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

## Auto-Tuner Improvement Plan

### Current State
- Tests frequency steps: 450, 500, 525, 550, 600, 625, 650, 700, 725, 750, 800 MHz
- Tests voltage steps: 1100, 1150, 1200, 1225, 1250, 1275, 1300 mV
- Mode limits:
  - Efficiency: max 625 MHz, 1175 mV
  - Balanced: max 700 MHz, 1200 mV
  - Hashrate: max 800 MHz, 1300 mV
- Test time: 20s stabilize + 45s test per combination
- Temperature limit: 65°C (autotune), 68°C (watchdog)

### Problems
1. ASICs vary widely - same settings produce different results
2. Efficiency mode doesn't push enough to find true optimal
3. No validation that target hashrate is actually achieved
4. Balanced mode complex - should be simpler
5. Long test times for many combinations

### Proposed Improvements

#### High Hashrate Mode
- **Goal**: Push limits, maximize hashrate regardless of efficiency
- Extend frequency range: test up to 850-900 MHz if ASIC supports
- Higher temp tolerance: allow up to 68°C during testing
- Voltage headroom: allow up to 1350 mV
- Validate hashrate: ensure measured hashrate matches expected
- Track hashrate stability (variance over test period)

#### Efficiency Mode
- **Goal**: Best J/TH while maintaining good actual hashrate
- Focus on efficiency (J/TH) but require minimum hashrate threshold
- Validate actual vs expected hashrate ratio (>90%)
- If hashrate underperforms, try higher voltage at same freq
- Adaptive testing: if good efficiency found, test nearby combinations more
- Shorter stabilization for initial screening, longer for promising settings

#### Balanced Mode
- **Goal**: Simple, safe, good results with device defaults
- Start from device default frequency (from config)
- Find minimum voltage that achieves target hashrate
- Don't push beyond default frequency
- Quick test: only vary voltage to find sweet spot
- Conservative temps: stay under 60°C
- Fallback: if can't achieve target, reduce frequency and retry

### Key Metrics to Track
1. **Expected hashrate**: freq × cores × asic_count / 1000
2. **Actual hashrate**: measured from ASIC
3. **Efficiency**: J/TH = (power × 1000) / hashrate_GH
4. **Hashrate ratio**: actual / expected (should be >90%)
5. **Temp headroom**: max_allowed - current
6. **Power headroom**: PSU limit - current

### Implementation Steps
1. Add hashrate validation (actual vs expected)
2. Add hashrate stability tracking (variance)
3. Implement mode-specific logic changes
4. Add adaptive testing for efficiency mode
5. Simplify balanced mode to voltage-only tuning
6. Add higher limits for hashrate mode
7. Update UI to show hashrate validation status

## Dual Pool Cluster Mode (v1.1.x) - NOT PRODUCTION READY

### Issues identified during testing:
1. **Race condition in slave share submission**: Slave reads `pool_id` from `current_work` which may have changed by the time share is found
2. **Attempted fix made things worse**: Adding per-job pool_id tracking led to 47.7% rejection rate and negative share counters
3. **Cross-chain pools**: Testing with BTC primary + BCH secondary added complexity

### Recommendations for future work:
- Test with same-coin pools first before cross-chain
- Per-job pool_id tracking needs proper implementation
- Share result routing and counter tracking needs redesign
