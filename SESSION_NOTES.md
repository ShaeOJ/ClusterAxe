# ClusterAxe Development Session Notes

## Session Date: December 28, 2025

---

## Summary of Changes

This session focused on improving the slave device UI, fixing chart display issues, and adding share tracking for slaves.

---

## 1. Slave Mode UI Improvements

### Problem
Slave devices were showing pool/block header information that doesn't apply to them since they connect to the master via ESP-NOW, not directly to pools.

### Solution
Added slave-specific UI components that show relevant cluster information.

### Files Modified

**`main/http_server/axe-os/src/app/components/home/home.component.ts`**
- Added `isSlaveMode: boolean` property (line 61)
- Updated `initClusterStatus()` to set `isSlaveMode = status.mode === 2` (line 134)

**`main/http_server/axe-os/src/app/components/home/home.component.html`**

Added Slave Mode Banner (lines 32-58):
```html
<!-- Slave Mode Banner -->
<ng-container *ngIf="clusterStatus$ | async as clusterStatus">
    <div *ngIf="clusterStatus.mode === 2" class="surface-card border-round p-3 mb-3">
        <!-- Shows: Slave Mode badge, connection status, ESP-NOW info, local hashrate -->
    </div>
</ng-container>
```

Updated Shares Card (lines 246-254):
- Shows "Shares to Master" with count from `clusterStatus.sharesSubmitted`
- Normal/Master mode shows standard shares display

Updated Pool Card (lines 531-563):
- Slave mode shows "Cluster Connection" card instead of Pool info
- Displays: Status, Transport (ESP-NOW), Hostname, Role badge

Updated Block Header Card (lines 724-747):
- Slave mode shows "Device Stats" instead of Block Header
- Displays: Hashrate, Temperature, Power, Frequency

Updated Rejection Display (lines 296-327):
- Slave mode shows master connection status
- Non-slave modes show standard rejection reasons

---

## 2. Chart Smoothing & Axis Fixes

### Problem
- Chart showed spikes and slopes on slave devices
- Y-axis showed raw numbers (900, 1100, 1200) instead of formatted values (TH/s, GH/s)

### Solution

**`main/http_server/axe-os/src/app/components/home/home.component.ts`**

Chart Dataset Configuration (lines 231-265):
```typescript
// Hashrate dataset
tension: 0.4,        // Increased smoothing (was 0.3)
pointRadius: 0,      // Hide data points (was 1)
spanGaps: true,      // Connect across missing data
order: 0,            // Draw last (on top)

// Temperature dataset
borderWidth: 1,      // Thinner line (was 2)
order: 1,            // Draw first (behind hashrate)
```

Y-Axis Configuration (lines 308-340):
```typescript
y: {  // Hashrate axis (left)
  beginAtZero: false,
  grace: '5%',
  maxTicksLimit: 6,  // Cleaner tick spacing
}

y2: {  // Temperature axis (right)
  suggestedMin: 30,  // Fixed range for consistency
  suggestedMax: 75,
  stepSize: 10,      // Nice round intervals (30, 40, 50, 60, 70)
  maxTicksLimit: 5,
}
```

Fixed `cbFormatValue()` function (lines 809-852):
- Made label matching more robust
- Checks against both enum values and string keys
- Properly formats hashrate with TH/s or GH/s suffix

---

## 3. Slave Share Tracking

### Problem
Slaves showed "Shares: 0" because they don't submit to pools - they send work to the master.

### Solution
Added share tracking for slaves that counts shares sent to master.

**`main/cluster/cluster.h`** (lines 377-382):
```c
/**
 * @brief Get slave share statistics
 * @param shares_found Output: number of shares found locally
 * @param shares_submitted Output: number of shares submitted to master
 */
void cluster_slave_get_shares(uint32_t *shares_found, uint32_t *shares_submitted);
```

**`main/cluster/cluster_slave.c`** (lines 706-714):
```c
void cluster_slave_get_shares(uint32_t *shares_found, uint32_t *shares_submitted)
{
    if (shares_found) {
        *shares_found = g_slave ? g_slave->shares_found : 0;
    }
    if (shares_submitted) {
        *shares_submitted = g_slave ? g_slave->shares_submitted : 0;
    }
}
```

**`main/http_server/http_server.c`** (lines 1387-1391):
```c
// Slave share statistics
uint32_t shares_found = 0, shares_submitted = 0;
cluster_slave_get_shares(&shares_found, &shares_submitted);
cJSON_AddNumberToObject(root, "sharesFound", shares_found);
cJSON_AddNumberToObject(root, "sharesSubmitted", shares_submitted);
```

**`main/http_server/axe-os/src/app/services/cluster.service.ts`** (lines 118-119):
```typescript
sharesFound?: number;
sharesSubmitted?: number;
```

---

## 4. Build Fix: Preprocessor Guards

### Problem
`http_proxy_to_slave` function was only defined in `#if CLUSTER_IS_MASTER` block but was being called from `POST_apply_profile` which is outside that block.

### Solution

**`main/http_server/http_server.c`**

Wrapped slave-related code in `POST_apply_profile` with preprocessor guards (lines 2191-2221):
```c
#if CLUSTER_IS_MASTER
    else if (strcmp(target, "slave") == 0 && slave_id >= 0) {
        // ... slave proxy code ...
    } else if (strcmp(target, "all") == 0) {
        // ... all slaves code ...
    }
#endif // CLUSTER_IS_MASTER
```

Also wrapped `slave_id` variable declaration (lines 2162-2176):
```c
#if CLUSTER_IS_MASTER
    int slave_id = -1;
#endif
// ...
#if CLUSTER_IS_MASTER
    cJSON *slave_item = cJSON_GetObjectItem(request, "slaveId");
    if (slave_item && cJSON_IsNumber(slave_item)) {
        slave_id = slave_item->valueint;
    }
#endif
```

---

## 5. Cluster Page Centering

### Problem
Cluster page content was not centered and needed tightening.

### Solution

**`main/http_server/axe-os/src/app/components/cluster/cluster.component.html`**

Added centering wrapper inside ng-container (lines 2-4):
```html
<ng-container *ngIf="clusterStatus$ | async as status; else loadingTemplate">
<div class="flex justify-content-center">
<div class="w-full" style="max-width: 900px;">
```

Closed wrapper before ng-container end (lines 541-543):
```html
</div>
</div>
</ng-container>
```

---

## Slave UI Final Layout

### Top Stats Row (4 cards):
1. **Hashrate** - Local device hashrate with error % and expected
2. **Efficiency** - J/Th calculation
3. **Shares to Master** - Count of shares submitted to master + connection status
4. **Best Difficulty** - All-time best with network diff %

### Bottom Cards Row:
1. **Cluster Connection** - Status, ESP-NOW transport, hostname, role badge
2. **Device Stats** - Hashrate, temperature, power, frequency

### Slave Mode Banner (top of page):
- Shows "Slave Mode" with connection status badge
- ESP-NOW cluster member info
- Local hashrate display

---

## Files Changed Summary

| File | Changes |
|------|---------|
| `cluster/cluster.h` | Added `cluster_slave_get_shares()` declaration |
| `cluster/cluster_slave.c` | Added `cluster_slave_get_shares()` implementation |
| `http_server/http_server.c` | Added shares to slave API, fixed preprocessor guards |
| `axe-os/.../home.component.ts` | Added `isSlaveMode`, chart improvements |
| `axe-os/.../home.component.html` | Complete slave UI overhaul |
| `axe-os/.../cluster.service.ts` | Added `sharesFound`, `sharesSubmitted` to interface |
| `axe-os/.../cluster.component.html` | Added centering wrapper |

---

## Build Commands

```bash
# Build Angular frontend
cd main/http_server/axe-os
npm run build

# Build ESP-IDF firmware
idf.py build

# Flash to device
idf.py -p COM3 flash
```

---

## Testing Notes

1. Master devices show normal pool/shares/block header information
2. Slave devices show:
   - Slave Mode banner with connection status
   - "Shares to Master" count (requires updated slave firmware)
   - Cluster Connection info instead of Pool
   - Device Stats instead of Block Header
3. Charts are smoother with better axis formatting
4. Cluster page is centered with max-width 900px

---

## Session Continuation: December 28, 2025 (Part 2)

---

## 6. Master Device Settings Section

### Problem
The cluster page only showed slave devices - no way to view/edit master device settings.

### Solution
Added a collapsible "Master Device" card on the cluster page.

**`cluster.component.html`**
- Master Device card with stats (hashrate, temp, power, frequency)
- Expandable panel with:
  - Device Info (model, firmware, hostname, efficiency)
  - Mining Settings (editable frequency 200-800 MHz, voltage 1000-1300 mV)
  - Cooling Settings (fan mode dropdown, fan speed slider)
  - "Include master in cluster auto-tune" checkbox

**`cluster.component.ts`**
- Added master device properties: `masterInfo`, `masterFrequency`, `masterVoltage`, etc.
- Added methods: `loadMasterInfo()`, `saveMasterFrequency()`, `saveMasterVoltage()`, etc.
- Master info loaded from SystemService on init

---

## 7. Autotune Mode Limits

### Problem
Autotune modes didn't have proper limits - user wanted specific constraints per mode.

### Solution
Updated `cluster_autotune.c` with mode-specific limits:

| Mode | Max Frequency | Max Voltage | Description |
|------|--------------|-------------|-------------|
| Efficiency | 550 MHz | 1175 mV | Best J/TH with low voltage |
| Balanced | 725 MHz | 1200 mV | Good performance, moderate power |
| Max Hashrate | 800 MHz | 1300 mV | Push for hashrate (temp limited) |

**Changes:**
```c
#define FREQ_MAX_MHZ_EFFICIENCY  550
#define FREQ_MAX_MHZ_BALANCED    725
#define FREQ_MAX_MHZ_HASHRATE    800

#define VOLTAGE_MAX_MV_EFFICIENCY  1175
#define VOLTAGE_MAX_MV_BALANCED    1200
#define VOLTAGE_MAX_MV_HASHRATE    1300

#define VOLTAGE_STEP_MV   25  // Smaller steps for finer tuning
```

Added `get_max_voltage_for_mode()` function.

Updated UI to show limits in mode dropdown and descriptions.

---

## 8. Autotune Inclusion Toggles

### Problem
User wanted ability to include/exclude devices from cluster-wide autotune.

### Solution

**Master Toggle:**
- Checkbox in expanded master device panel
- `masterIncludeInAutotune` property

**Slave Toggles:**
- Checkbox in each slave's expanded config panel
- `slaveAutotuneIncluded` Set tracks which slaves are included
- All slaves included by default

---

## 9. Voltage Display Fix

### Problem
Autotune showed "800 MHz @ 27521 mV" - voltage way too high. Slaves showed 7120 mV.

### Root Cause
`cluster_get_core_voltage()` was reading INPUT voltage (~5V) instead of CORE voltage (~1.2V).

### Solution

**`cluster_integration.c`:**
```c
#include "power/vcore.h"

uint16_t cluster_get_core_voltage(void)
{
    if (!g_global_state) {
        return 0;
    }
    // Get actual core voltage in mV from VCORE module
    int16_t voltage_mv = VCORE_get_voltage_mv(g_global_state);
    return (voltage_mv > 0) ? (uint16_t)voltage_mv : 0;
}
```

**UI Validation (`cluster.component.ts`):**
```typescript
// If voltage > 1500mV, it's wrong data from old firmware
if (config.coreVoltage > 1500) {
    this.editVoltage = 1200; // Default to safe value
    console.warn(`Slave reported invalid voltage - rebuild slave firmware`);
}
```

**Note:** Slaves need to be rebuilt and reflashed to report correct voltage.

---

## 10. Chart Y-Axis Auto-Scaling

### Problem
Hashrate Y-axis scale was too large, data didn't fill the chart area.

### Solution

**`home.component.ts`:**
```typescript
y: {
    beginAtZero: false,
    ticks: { maxTicksLimit: 6 },
    // Tightly fit the data with minimal padding
    afterDataLimits: (scale: any) => {
        const range = scale.max - scale.min;
        if (range > 0) {
            const padding = range * 0.05;  // Just 5% padding
            scale.min = Math.max(0, scale.min - padding);
            scale.max = scale.max + padding;
        } else {
            // Flat line - create ±5% range
            scale.min = Math.max(0, scale.max * 0.95);
            scale.max = scale.max * 1.05;
        }
    }
}
```

---

## 11. ESP-NOW Slave Autotune UI

### Problem
"Click refresh to load autotune status" stayed empty for ESP-NOW slaves.

### Root Cause
ESP-NOW slaves don't have IP addresses, so HTTP proxy fails.

### Solution
Updated UI to show helpful message:
- ESP-NOW slaves: "ESP-NOW slaves require direct access for autotune"
- Slaves with IP: Shows Start Auto-Tune button + link to slave web UI

---

## Files Changed (Part 2)

| File | Changes |
|------|---------|
| `cluster_autotune.c` | Mode-specific freq/voltage limits, smaller step sizes |
| `cluster_autotune.h` | Updated mode enum comments with limits |
| `cluster_integration.c` | Fixed `cluster_get_core_voltage()` to use VCORE |
| `cluster.component.ts` | Master device methods, autotune toggles, voltage validation |
| `cluster.component.html` | Master device card, autotune toggles, mode descriptions |
| `home.component.ts` | Chart Y-axis auto-scaling |

---

## Git Commits

```
f273330 Improve slave UI and chart display
ea9591b Fix autotune build errors
cff882d Add auto-tuning feature with oscilloscope UI
2271262 Add master device settings, autotune mode limits, and UI improvements
```

---

## Remaining Tasks

1. **Rebuild slave firmware** - Slaves need reflashing to report correct voltage
2. ~~**Test autotune** - Verify mode limits work correctly~~ Fixed - see below
3. **Chart fine-tuning** - May need further adjustment based on real data

---

## Session Continuation: December 29, 2025

---

## 12. Autotune Not Applying Settings Fix

### Problem
Autotune showed "Testing - 2%, 400 MHz @ 1075 mV, Test 3/126" but the master device still showed "800 MHz". The autotune was NOT actually applying the frequency/voltage changes during testing.

### Root Cause
Two issues in `cluster_autotune_apply_settings()`:

1. **Wrong NVS function for frequency**: The code used `nvs_config_set_u16()` to save frequency, but `power_management_task.c` reads it with `nvs_config_get_float()`. This type mismatch meant NVS wasn't saving the frequency correctly.

2. **Missing state update**: The code didn't update `GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value` directly, so the UI showed stale data until the power management task detected the (broken) NVS change.

### Solution

**`main/cluster/cluster_autotune.c`** (lines 358-368):

```c
// BEFORE (broken):
nvs_config_set_u16(NVS_CONFIG_ASIC_FREQUENCY, frequency_mhz);
nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, voltage_mv);

// AFTER (fixed):
// Save to NVS - NOTE: frequency must be saved as float, not u16!
nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY, (float)frequency_mhz);
nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, voltage_mv);

// Update POWER_MANAGEMENT_MODULE directly so UI reflects changes immediately
GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value = (float)frequency_mhz;
// Also update expected hashrate calculation
GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate =
    (float)frequency_mhz * GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count *
    GLOBAL_STATE->DEVICE_CONFIG.family.asic_count / 1000.0f;
```

Also added `#include "device_config.h"` to access ASIC configuration for hashrate calculation.

### Testing
After rebuilding firmware:
1. Start autotune in any mode (Efficiency, Balanced, Max Hashrate)
2. Watch the device frequency change in real-time as tests progress
3. UI should show current test frequency (e.g., "400 MHz") instead of the old value
4. Hashrate should stabilize at the tested frequency before moving to next test

---

## Files Changed (December 29, 2025)

| File | Changes |
|------|---------|
| `cluster_autotune.c` | Fixed NVS save type (u16 -> float), added direct state updates |

---

## Build Commands

```bash
# In ESP-IDF environment:
idf.py build
idf.py -p COM3 flash
```

---

## 13. Complete Autotune Algorithm Rewrite

### Requirements
User specified exact tuning parameters:
- Base start: 450 MHz, 1100 mV
- Custom frequency steps: 450, 500, 525, 550, 600, 625, 650, 700, 725, 750, 800 MHz
- Custom voltage steps: 1100, 1150, 1200, 1225, 1250, 1275, 1300 mV
- Temperature target: 65°C max
- Input voltage protection: drop to 1100 mV if Vin < 4.9V

### Mode Limits

| Mode | Max Frequency | Max Voltage | Description |
|------|--------------|-------------|-------------|
| Efficiency | 625 MHz | 1175 mV | Best J/TH - lowest power |
| Balanced | 700 MHz | 1200 mV | Good balance |
| Max Hashrate | 800 MHz | 1300 mV | Highest hashrate |

### Implementation

**`main/cluster/cluster_autotune.c`** - Complete rewrite:

```c
// Custom frequency steps (not linear)
static const uint16_t FREQ_STEPS[] = {450, 500, 525, 550, 600, 625, 650, 700, 725, 750, 800};

// Custom voltage steps
static const uint16_t VOLTAGE_STEPS[] = {1100, 1150, 1200, 1225, 1250, 1275, 1300};

// Temperature limits
#define TEMP_TARGET_C         65    // Target max temperature

// Input voltage protection
#define VIN_MIN_SAFE          4.9f  // Minimum safe input voltage
#define VOLTAGE_SAFE_MV       1100  // Drop to this if Vin too low
```

**Key Features:**
1. **Custom step values** - Not linear steps, specific values for fine-tuning
2. **Temperature monitoring** - Checks every 5 seconds, rejects settings > 65°C
3. **Input voltage protection** - If Vin < 4.9V, immediately drops core voltage to 1100 mV
4. **Mode-specific limits** - Each mode has frequency and voltage caps
5. **Best-tracking** - Tracks best efficiency/hashrate/balanced score per mode

**Algorithm Flow:**
1. Start at base (450 MHz, 1100 mV)
2. Stabilize for 20 seconds
3. For each freq/voltage combination (within mode limits):
   - Apply settings
   - Stabilize 10 seconds
   - Check temperature - skip if > 65°C
   - Test for 45 seconds, collecting samples
   - Check input voltage periodically
   - Calculate efficiency (J/TH)
   - Update best if better based on mode
4. Apply best settings when complete

**Files Changed:**
- `cluster_autotune.c` - Complete rewrite with new algorithm
- `cluster_autotune.h` - Updated mode comments
- `cluster.component.ts` - Updated UI mode labels

### Test Count by Mode

| Mode | Freq Steps | Voltage Steps | Total Tests |
|------|-----------|---------------|-------------|
| Efficiency | 5 (450-625) | 3 (1100-1175) | 15 |
| Balanced | 7 (450-700) | 4 (1100-1200) | 28 |
| Max Hashrate | 11 (450-800) | 7 (1100-1300) | 77 |

### Input Voltage Protection

```c
static bool check_input_voltage_protection(void)
{
    float vin = get_input_voltage();

    if (vin < VIN_MIN_SAFE) {  // 4.9V
        // Immediately drop to safe voltage
        VCORE_set_voltage(GLOBAL_STATE, VOLTAGE_SAFE_MV / 1000.0f);
        nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, VOLTAGE_SAFE_MV);
        return false;
    }
    return true;
}
```

This prevents the device from crashing due to undervoltage when the power supply can't keep up with high-demand settings.

---

## Current Status (December 29, 2025)

### COMPLETED WORK

#### Frontend (Angular)
- [x] Slave mode UI improvements (banner, shares to master, cluster connection card)
- [x] Chart smoothing and Y-axis auto-scaling
- [x] Temperature axis rounding to whole numbers
- [x] Cluster page centering (max-width 900px)
- [x] Master device settings panel on cluster page
- [x] Autotune mode dropdown with updated limits
- [x] Autotune inclusion toggles for master/slaves
- [x] Share rejection explanations (including "105 unknown")
- [x] **Angular frontend built** - `npm run build` completed successfully

#### Backend (C/ESP-IDF)
- [x] Slave share tracking (`cluster_slave_get_shares()`)
- [x] Fixed `cluster_get_core_voltage()` to use VCORE instead of input voltage
- [x] Fixed autotune NVS save (changed `nvs_config_set_u16` to `nvs_config_set_float` for frequency)
- [x] Direct POWER_MANAGEMENT_MODULE state updates in autotune
- [x] **Complete autotune algorithm rewrite** with:
  - Custom frequency steps: 450, 500, 525, 550, 600, 625, 650, 700, 725, 750, 800 MHz
  - Custom voltage steps: 1100, 1150, 1200, 1225, 1250, 1275, 1300 mV
  - Mode limits (Efficiency: 625/1175, Balanced: 700/1200, Max: 800/1300)
  - Temperature target: 65°C
  - Input voltage protection: drops to 1100mV if Vin < 4.9V

### PENDING WORK

#### Must Do
- [ ] **Build ESP-IDF firmware** - Run `idf.py build` in ESP-IDF environment
- [ ] **Flash firmware to device** - Run `idf.py -p COM3 flash`
- [ ] **Test autotune** - Verify frequency/voltage changes apply correctly

#### Nice to Have
- [ ] Rebuild slave firmware for correct voltage reporting
- [ ] Test input voltage protection (requires PSU that can't supply enough current)

### KEY FILES MODIFIED THIS SESSION

| File | Status | Description |
|------|--------|-------------|
| `cluster_autotune.c` | **REWRITTEN** | Complete new autotune algorithm |
| `cluster_autotune.h` | Modified | Updated mode comments |
| `cluster.component.ts` | Modified | Updated autotune mode labels |
| `cluster_integration.c` | Modified | Fixed voltage reading |
| `home.component.ts` | Modified | Chart improvements |
| `http_server.c` | Modified | Added efficiency to API endpoints |

### API ADDITIONS

**`/api/system/info`** - Added:
- `efficiency` - Device efficiency in J/TH

**`/api/cluster/status`** - Added:
- `totalPower` - Sum of master + all slave power (Watts)
- `totalEfficiency` - Cluster-wide efficiency (J/TH)

### UI CHANGES

**Cluster Page Width:**
- Changed from `max-width: 900px` to `max-width: 1400px`
- Page now fills more of the available screen space while staying centered

### BUILD COMMANDS

```bash
# 1. Angular frontend (ALREADY DONE)
cd main/http_server/axe-os
npm run build

# 2. ESP-IDF firmware (NEEDS TO BE DONE)
# Open ESP-IDF terminal/PowerShell, then:
cd C:\Users\ShaeOJ\Documents\GitHub\ClusterAxe
idf.py build
idf.py -p COM3 flash
```

### AUTOTUNE QUICK REFERENCE

**Frequency Steps:** 450 → 500 → 525 → 550 → 600 → 625 → 650 → 700 → 725 → 750 → 800

**Voltage Steps:** 1100 → 1150 → 1200 → 1225 → 1250 → 1275 → 1300

**Mode Limits:**
- Efficiency: ≤625 MHz, ≤1175 mV
- Balanced: ≤700 MHz, ≤1200 mV
- Max Hashrate: ≤800 MHz, ≤1300 mV

**Safety:**
- Temperature target: 65°C (skips settings that exceed this)
- Input voltage protection: If Vin < 4.9V → core voltage drops to 1100 mV

**Timing:**
- Initial stabilization: 20 seconds
- Per-test stabilization: 10 seconds
- Test duration: 45 seconds
- Temp check interval: 5 seconds

---

---

## Session Continuation: December 29, 2025 (Part 2)

### Changes Made

1. **Fixed master hashrate display** - Was showing 15.03 GH/s instead of 1503 GH/s
   - `masterInfo.hashRate` is in GH/s, but `formatHashrate()` expects GH/s * 100
   - Fixed by multiplying by 100: `formatHashrate((masterInfo.hashRate || 0) * 100)`

2. **Added efficiency to APIs**
   - `/api/system/info` now returns `efficiency` (J/TH)
   - `/api/cluster/status` now returns `totalPower` and `totalEfficiency`

3. **Widened cluster page** - Changed from 900px to 1400px max-width

4. **Added autotune status badges** to cluster page
   - Master shows spinning cog with current frequency when tuning
   - Master shows green "Locked" badge when complete (stateCode === 5)
   - Slaves show same badges

### Git Commits This Session
```
e799753 Add autotune status badges to cluster page
761be6d Fix master device hashrate display on cluster page
e2e1fbc Rewrite autotune algorithm and add API efficiency metrics
```

---

## AUTOTUNE - NEEDS FINE TUNING

### Current Implementation

**File:** `main/cluster/cluster_autotune.c`

**Frequency Steps:** 450, 500, 525, 550, 600, 625, 650, 700, 725, 750, 800 MHz

**Voltage Steps:** 1100, 1150, 1200, 1225, 1250, 1275, 1300 mV

**Mode Limits:**
| Mode | Max Freq | Max Voltage | Tests |
|------|----------|-------------|-------|
| Efficiency | 625 MHz | 1175 mV | ~15 |
| Balanced | 700 MHz | 1200 mV | ~28 |
| Max Hashrate | 800 MHz | 1300 mV | ~77 |

**Timing:**
- Initial stabilization: 20 seconds
- Per-test stabilization: 10 seconds (after applying settings)
- Test duration: 45 seconds (collecting samples)
- Temp check interval: 5 seconds

**Safety:**
- Temperature target: 65°C max
- Input voltage protection: If Vin < 4.9V → drops core voltage to 1100 mV

### Potential Areas to Fine-Tune

1. **Timing values** - May need adjustment based on real-world testing
   - `AUTOTUNE_STABILIZE_TIME_MS` (20000) - Initial stabilization
   - `AUTOTUNE_TEST_TIME_MS` (45000) - Test duration per setting
   - Stabilization after setting change (currently 10 seconds)

2. **Step values** - Current steps may be too coarse or too fine
   - Frequency steps: 450, 500, 525, 550, 600, 625, 650, 700, 725, 750, 800
   - Voltage steps: 1100, 1150, 1200, 1225, 1250, 1275, 1300

3. **Mode scoring** - How "best" is determined
   - Efficiency mode: Lowest J/TH
   - Hashrate mode: Highest GH/s
   - Balanced mode: `hashrate / efficiency` score

4. **Temperature handling**
   - Currently skips any setting that exceeds 65°C
   - Could be more aggressive (push closer to limit in hashrate mode)

5. **Test order** - Currently tests low freq → high freq, low voltage → high voltage
   - Could optimize by starting near expected optimal values

### Autotune API Reference

**GET `/api/cluster/autotune/status`**
```json
{
  "state": "testing",
  "stateCode": 2,
  "mode": "efficiency",
  "enabled": true,
  "running": true,
  "currentFrequency": 525,
  "currentVoltage": 1150,
  "bestFrequency": 500,
  "bestVoltage": 1100,
  "bestEfficiency": 21.5,
  "bestHashrate": 485.2,
  "progress": 45,
  "testsCompleted": 7,
  "testsTotal": 15,
  "testDuration": 32000,
  "totalDuration": 180000
}
```

**State codes:** 0=idle, 1=starting, 2=testing, 3=adjusting, 4=stabilizing, 5=locked, 6=error

**POST `/api/cluster/autotune`**
```json
{"action": "start", "mode": "efficiency"}
{"action": "stop", "apply": true}
```

### Key Functions in cluster_autotune.c

- `cluster_autotune_start(mode)` - Starts autotune with specified mode
- `cluster_autotune_stop(apply_best)` - Stops autotune, optionally applies best settings
- `cluster_autotune_apply_settings(freq, voltage)` - Applies freq/voltage to ASIC
- `cluster_autotune_task()` - Main autotune task loop
- `check_input_voltage_protection()` - Checks Vin and drops voltage if too low

### TODO for Autotune Fine-Tuning

- [ ] Test on actual hardware to verify settings apply correctly
- [ ] Monitor hashrate stabilization - is 20s/45s enough?
- [ ] Check if temperature readings are accurate during rapid changes
- [ ] Verify input voltage protection triggers correctly
- [ ] Consider adding more granular voltage steps (e.g., 1125, 1175)
- [ ] Consider smarter test ordering (start near middle, binary search)
- [ ] Add ability to resume autotune after power loss
- [ ] Test with wireless monitor to verify API responses

---

## How to Continue This Work

If this session is lost and you need to continue:

1. **Read this file** (`SESSION_NOTES.md`) for full context
2. **Check git status** to see uncommitted changes
3. **Build firmware** if not already done:
   ```bash
   idf.py build
   idf.py -p COM3 flash
   ```
4. **Test autotune** on device to verify it works

The main autotune code is in:
- `main/cluster/cluster_autotune.c` - Algorithm implementation
- `main/cluster/cluster_autotune.h` - Types and API declarations

The frontend autotune UI is in:
- `main/http_server/axe-os/src/app/components/cluster/cluster.component.ts`
- `main/http_server/axe-os/src/app/components/cluster/cluster.component.html`

---

## Session Continuation: December 29, 2025 (Part 3)

---

### 14. Added Efficiency Stats to Cluster Page

Added efficiency display (J/TH) with purple gauge icon to both master and slave device cards on the cluster page.

**Changes:**
- Master device shows efficiency after power stat
- Slave devices show efficiency calculated from power/hashrate

---

### 15. Added Frequency and Voltage Stats to Slave Devices

Slave device cards now display:
- Frequency (MHz) with microchip icon
- Core Voltage (mV) with bolt icon
- Efficiency (J/TH) with gauge icon

---

### 16. Fixed Button Icon Colors

Fixed gray/red button icon issue by adding `p-button-secondary` class to action buttons on cluster page.

---

### 17. Fixed Master Device Stats Not Updating

**Problem:** Master hashrate was staying constant while slaves updated in real-time.

**Root Cause:** `loadMasterInfo()` was only called once on init.

**Solution:** Now call `loadMasterInfo()` on every cluster status poll:
```typescript
if (status.mode === 1) {
  this.loadMasterInfo(); // Called on every poll, not just init
  // ...
}
```

---

### 18. Fixed Chart Axis Showing Duplicate Values

**Problem:** Power axis showed "22W 22W" when data had small fluctuations.

**Root Cause:** Auto-scaling with minimal range caused duplicate tick marks.

**Solution:** Added minimum 5% range and increased padding to 20%:
```typescript
// Ensure minimum range of 5% of max value
const minRange = y1Max * 0.05 || 1;
if (y1Range < minRange) {
  y1Range = minRange;
}
const y1Padding = y1Range * 0.2; // Increased from 0.1
```

---

### 19. Added Cluster Power and Efficiency to Chart Dropdown

**Problem:** User wanted to track cluster-wide power and efficiency over time on the home page chart.

**Solution:** Added new chart options that only appear when device is in master mode.

**Files Changed:**

**`eChartLabel.ts`** - Added enum values:
```typescript
clusterPower = 'Cluster Power',
clusterEfficiency = 'Cluster Efficiency',
```

**`home.component.ts`**:
- Added `clusterPowerData: number[]` and `clusterEfficiencyData: number[]` arrays
- Updated `initClusterStatus()` to track cluster power and efficiency from API
- Updated `getDataForLabel()` to return cluster power/efficiency values
- Updated `getSuggestedMaxForLabel()` with reasonable max values
- Updated `getSettingsForLabel()` with suffix (W, J/TH) and precision
- Updated `cbFormatValue()` to format cluster power and efficiency
- Updated `dataSourceLabels()` to filter cluster options (only show in master mode)

**`cluster.service.ts`**:
- Added `totalPower` and `totalEfficiency` to `IClusterStatus` interface
- Updated mock data for development

---

### 20. Wireless Pip-Boy Display - Autotune 404 Issue

**Problem:** User's wireless ESP32 display (Pip-Boy edition) was getting 404 errors when fetching `/api/cluster/autotune/status` and `/api/cluster/profiles`.

**Root Cause:** The firmware running on the ClusterAxe master (10.0.0.112) is **v1.0.0** - built before the autotune API endpoints were added.

**Solution:** User needs to rebuild and flash the firmware:
```bash
cd C:\Users\ShaeOJ\Documents\GitHub\ClusterAxe
idf.py build
idf.py -p COM<X> flash
```

The autotune endpoints are wrapped in `#if CLUSTER_ENABLED` and only available when built as cluster master (`CONFIG_CLUSTER_MODE_MASTER` in menuconfig).

---

### Git Commits This Session (Part 3)

```
a08a2bd Add cluster power and efficiency to chart dropdown options
f770b97 Fix chart axis showing duplicate values when data has small fluctuations
82e62e4 Fix master device stats not updating in real-time on cluster page
21e1431 Add frequency and voltage stats to slave devices on cluster page
98e0a46 Add efficiency stats and fix button icon colors on cluster page
```

---

### Current Device Status (10.0.0.112)

```
Firmware: ClusterAxe-v1.0.0 (needs rebuild)
Frequency: 700 MHz @ 1150 mV
Hashrate: ~1.6 TH/s (this device)
Cluster: 3 workers, ~5.7 TH/s total
Pool: firepool.ca:5333
Efficiency: 13.2 J/TH
Temperature: 52°C (VR: 49°C)
Power: 21.6W
```

---

### Autotune API Endpoints Reference

**GET `/api/cluster/autotune/status`**
```json
{
  "state": "testing",
  "stateCode": 2,
  "mode": "efficiency",
  "enabled": true,
  "running": true,
  "currentFrequency": 525,
  "currentVoltage": 1200,
  "bestFrequency": 500,
  "bestVoltage": 1150,
  "bestEfficiency": 17.5,
  "bestHashrate": 1.2,
  "progress": 45,
  "testsCompleted": 9,
  "testsTotal": 20,
  "testDuration": 30000,
  "totalDuration": 120000,
  "error": null
}
```

**State Codes:**
| Code | State | Description |
|------|-------|-------------|
| 0 | idle | Not running |
| 1 | starting | Initializing |
| 2 | testing | Testing current freq/voltage |
| 3 | adjusting | Moving to next step |
| 4 | stabilizing | Waiting for stable readings |
| 5 | locked | Tuning complete, best settings applied |
| 6 | error | Error occurred |

**POST `/api/cluster/autotune`**
```bash
# Start autotune
curl -X POST http://<ip>/api/cluster/autotune \
  -H "Content-Type: application/json" \
  -d '{"action": "start", "mode": "efficiency"}'

# Stop autotune (apply best settings)
curl -X POST http://<ip>/api/cluster/autotune \
  -H "Content-Type: application/json" \
  -d '{"action": "stop", "applyBest": true}'
```

**GET `/api/cluster/profiles`** - Get saved tuning profiles

---

### PENDING WORK

| Task | Priority | Notes |
|------|----------|-------|
| **Build & flash firmware** | HIGH | Required for autotune API to work |
| Test autotune on device | HIGH | Verify settings apply correctly |
| Update Pip-Boy display | MEDIUM | Will work after firmware flash |
| Rebuild slave firmware | LOW | For correct voltage reporting |

---

### Files Changed Summary (December 29, 2025 - Part 3)

| File | Changes |
|------|---------|
| `home.component.ts` | Chart axis fix, cluster power/efficiency tracking |
| `cluster.component.ts` | Master stats refresh fix |
| `cluster.component.html` | Efficiency, freq, voltage stats for slaves |
| `cluster.service.ts` | Added totalPower/totalEfficiency to interface |
| `eChartLabel.ts` | Added clusterPower, clusterEfficiency enums |

---

### How to Continue

1. **Flash firmware to master device:**
   ```bash
   cd C:\Users\ShaeOJ\Documents\GitHub\ClusterAxe
   idf.py build
   idf.py -p COM<X> flash
   ```

2. **After flashing, verify autotune works:**
   ```bash
   curl http://10.0.0.112/api/cluster/autotune/status
   ```

3. **Pip-Boy display should then work** - no code changes needed on display, just needs the API to exist

4. **If context is lost**, read this SESSION_NOTES.md file for full history

---

## Session Continuation: December 30, 2025

---

### 21. Master-Controlled Slave Autotune via HTTP

**Problem:** Autotune was only tuning the master device, not slaves. The previous setup had the master coordinate slave tuning via HTTP API calls.

**Solution:** Implemented full slave autotune control from master using HTTP PATCH requests.

---

### Implementation Details

**`main/cluster/cluster_autotune.c`** - Major additions:

1. **HTTP Client for Slave Control:**
```c
#if CLUSTER_IS_MASTER
#include "esp_http_client.h"
#include "cluster.h"
#endif
```

2. **Slave Tracking in State:**
```c
// Slave autotune tracking (master only)
bool include_master;
uint8_t slave_include_mask;  // Bitmask of slaves to include
int8_t current_device;       // -1 = master, 0-7 = slave index
```

3. **Per-Slave Results:**
```c
typedef struct {
    uint16_t best_frequency;
    uint16_t best_voltage;
    float best_efficiency;
    float best_hashrate;
    bool valid;
} slave_autotune_result_t;

static slave_autotune_result_t g_slave_results[CONFIG_CLUSTER_MAX_SLAVES] = {0};
```

4. **HTTP Helper to Apply Settings to Slave:**
```c
static esp_err_t apply_settings_to_slave(const char *ip_addr, uint16_t freq_mhz, uint16_t voltage_mv)
{
    char url[64];
    snprintf(url, sizeof(url), "http://%s/api/system", ip_addr);

    char post_data[128];
    snprintf(post_data, sizeof(post_data),
             "{\"frequency\":%d,\"coreVoltage\":%d}", freq_mhz, voltage_mv);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_PATCH,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    return (err == ESP_OK && status == 200) ? ESP_OK : ESP_FAIL;
}
```

5. **Get Slave Stats from Cluster Status:**
```c
static bool get_slave_stats(uint8_t slave_id, float *hashrate, float *power, float *temp)
{
    cluster_status_t status;
    if (cluster_get_status(&status) != ESP_OK) return false;

    if (slave_id >= status.slave_count) return false;

    *hashrate = status.slaves[slave_id].hash_rate;
    *power = status.slaves[slave_id].power;
    *temp = status.slaves[slave_id].temperature;
    return true;
}
```

6. **Full Slave Autotune Algorithm:**
```c
static void autotune_slave_device(uint8_t slave_id, autotune_mode_t mode)
{
    g_autotune.current_device = slave_id;
    const char *ip = get_slave_ip(slave_id);

    // Same algorithm as master:
    // - Iterate through freq/voltage combinations
    // - Apply settings via HTTP PATCH to slave's /api/system
    // - Read stats from cluster status (hashrate, power, temp)
    // - Track best settings per slave
    // - Apply best settings when complete
}
```

7. **Main Task Iteration Through Slaves:**
```c
#if CLUSTER_IS_MASTER
if (g_autotune.task_running && g_autotune.slave_include_mask != 0) {
    for (int i = 0; i < CONFIG_CLUSTER_MAX_SLAVES && g_autotune.task_running; i++) {
        if (!(g_autotune.slave_include_mask & (1 << i))) continue;

        const char *ip = get_slave_ip(i);
        if (!ip) continue;

        autotune_slave_device(i, g_autotune.status.mode);
    }
}
#endif
```

---

### Device Selection API

**New Functions in `cluster_autotune.h`:**
```c
void cluster_autotune_set_include_master(bool include);
void cluster_autotune_set_slave_mask(uint8_t mask);
void cluster_autotune_set_slave_include(uint8_t slave_id, bool include);
int8_t cluster_autotune_get_current_device(void);
```

---

### HTTP API Updates

**`main/http_server/http_server.c`:**

**POST `/api/cluster/autotune`** - New parameters:
```json
{
  "action": "start",
  "mode": "efficiency",
  "includeMaster": true,
  "slaveMask": 255,
  "includeSlaves": [0, 1, 2]
}
```

- `includeMaster` (bool) - Whether to autotune master device
- `slaveMask` (number) - Bitmask of slaves to include (bit 0 = slave 0, etc.)
- `includeSlaves` (array) - Alternative: array of slave IDs to include

**GET `/api/cluster/autotune/status`** - New field:
```json
{
  "currentDevice": -1
}
```

- `currentDevice`: -1 = master, 0-7 = slave index being tuned

---

### Slave IP Clickable Link

**`main/http_server/axe-os/src/app/components/cluster/cluster.component.html`:**

Changed slave IP from plain text to clickable link:
```html
<a *ngIf="slave.ipAddr"
   [href]="'http://' + slave.ipAddr"
   target="_blank"
   class="text-primary no-underline hover:underline"
   (click)="$event.stopPropagation()">{{slave.ipAddr}}</a>
```

---

### Version Bump

Bumped version to **v1.0.1** in:
- `main/http_server/axe-os/generate-version.js` (fallback version)

---

### Testing Commands

```bash
# Start autotune on master + all slaves
curl -X POST http://10.0.0.112/api/cluster/autotune \
  -H "Content-Type: application/json" \
  -d '{"action":"start","mode":"efficiency","includeMaster":true,"slaveMask":255}'

# Check status (see currentDevice field)
curl http://10.0.0.112/api/cluster/autotune/status

# Start autotune on specific slaves only (slaves 0 and 2)
curl -X POST http://10.0.0.112/api/cluster/autotune \
  -H "Content-Type: application/json" \
  -d '{"action":"start","mode":"balanced","includeMaster":false,"includeSlaves":[0,2]}'
```

---

### Autotune Flow (Cluster-Wide)

1. User starts autotune via POST with device selection
2. If `includeMaster` is true:
   - Autotune master device first (same algorithm as before)
   - `currentDevice` = -1 during this phase
3. For each slave in `slaveMask`:
   - Set `currentDevice` = slave_id
   - Apply freq/voltage via HTTP PATCH to `http://<slave_ip>/api/system`
   - Read stats from `cluster_get_status()` (hashrate, power, temp)
   - Track best settings per slave
   - Apply best settings when complete
4. When all devices done, state → LOCKED

---

### Files Changed (December 30, 2025)

| File | Changes |
|------|---------|
| `cluster_autotune.c` | HTTP slave control, per-slave tracking, device iteration |
| `cluster_autotune.h` | Device selection function declarations |
| `http_server.c` | Device selection params in POST, currentDevice in GET |
| `cluster.component.html` | Slave IP clickable link |
| `generate-version.js` | Bumped fallback to v1.0.1 |

---

### 22. Safety Watchdog Feature

**Purpose:** Continuous background monitoring to protect devices from overheating and undervoltage.

**Behavior:**
- Runs as independent FreeRTOS task (higher priority than autotune)
- Checks every 5 seconds
- **Temperature > 65°C**: Drops core voltage one step (e.g., 1200 → 1150 mV)
- **Input Voltage < 4.9V**: Drops BOTH frequency AND voltage one step
- Continues stepping down until conditions are safe
- On master: also monitors all connected slaves via cluster status and sends HTTP PATCH to reduce their settings

**Frequency Steps:** 450 → 500 → 525 → 550 → 600 → 625 → 650 → 700 → 725 → 750 → 800 MHz

**Voltage Steps:** 1100 → 1150 → 1200 → 1225 → 1250 → 1275 → 1300 mV

---

### Implementation Details

**`main/cluster/cluster_autotune.c`** - Added:

1. **Watchdog Configuration:**
```c
#define WATCHDOG_CHECK_INTERVAL_MS    5000    // Check every 5 seconds
#define WATCHDOG_TASK_STACK_SIZE      3072
#define WATCHDOG_TASK_PRIORITY        6       // Higher priority than autotune
```

2. **Watchdog State:**
```c
// In g_autotune struct
bool watchdog_enabled;
bool watchdog_running;
TaskHandle_t watchdog_task_handle;
uint16_t watchdog_last_freq;     // Track for gradual reduction
uint16_t watchdog_last_voltage;  // Track for gradual reduction
```

3. **Helper Functions:**
```c
static uint16_t get_lower_freq_step(uint16_t current_freq);
static uint16_t get_lower_voltage_step(uint16_t current_voltage);
```

4. **Watchdog Task:**
```c
static void cluster_watchdog_task(void *pvParameters)
{
    while (g_autotune.watchdog_running) {
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_CHECK_INTERVAL_MS));

        // Check master device
        if (current_temp > TEMP_TARGET_C) {
            // Drop voltage one step
        }
        if (current_vin < VIN_MIN_SAFE) {
            // Drop freq AND voltage one step
        }

        // Check slaves (master only)
        #if CLUSTER_IS_MASTER
        for (int i = 0; i < CONFIG_CLUSTER_MAX_SLAVES; i++) {
            // Monitor slave temp, send HTTP PATCH if needed
        }
        #endif
    }
}
```

**`main/cluster/cluster_autotune.h`** - Added:
```c
esp_err_t cluster_autotune_watchdog_enable(bool enable);
bool cluster_autotune_watchdog_is_enabled(void);
bool cluster_autotune_watchdog_is_running(void);
```

**`main/http_server/http_server.c`** - Added:

GET `/api/cluster/autotune/status` returns:
```json
{
  "watchdogEnabled": true,
  "watchdogRunning": true
}
```

POST `/api/cluster/autotune` accepts:
```json
{"action": "enableWatchdog"}
{"action": "disableWatchdog"}
```

---

### Frontend UI Changes

**`cluster.component.ts`** - Added:
```typescript
public watchdogEnabled = false;
public watchdogLoading = false;

toggleWatchdog(): void {
    this.watchdogLoading = true;
    const newState = !this.watchdogEnabled;
    this.clusterService.setWatchdog('', newState).subscribe({...});
}
```

**`cluster.component.html`** - Added toggle in Auto-Tune section:
```html
<!-- Safety Watchdog Toggle -->
<div class="flex align-items-center gap-2 ml-0 md:ml-3 mt-3 md:mt-0 border-left-1 surface-border pl-3">
    <p-inputSwitch [(ngModel)]="watchdogEnabled"
                   (onChange)="toggleWatchdog()"
                   [disabled]="watchdogLoading">
    </p-inputSwitch>
    <div class="flex flex-column">
        <span class="font-medium text-sm">
            <i class="pi pi-shield mr-1" [ngClass]="{'text-green-500': watchdogEnabled, 'text-500': !watchdogEnabled}"></i>
            Safety Watchdog
        </span>
        <span class="text-500 text-xs">Auto-reduce if temp >65°C or Vin <4.9V</span>
    </div>
</div>
```

**`cluster.service.ts`** - Added:
```typescript
// Interface additions
watchdogEnabled?: boolean;
watchdogRunning?: boolean;

// Method
public setWatchdog(uri: string = '', enabled: boolean): Observable<any> {
    const action = enabled ? 'enableWatchdog' : 'disableWatchdog';
    return this.httpClient.post(`${uri}/api/cluster/autotune`, { action });
}
```

---

### Testing Commands

```bash
# Enable watchdog
curl -X POST http://10.0.0.112/api/cluster/autotune \
  -H "Content-Type: application/json" \
  -d '{"action":"enableWatchdog"}'

# Disable watchdog
curl -X POST http://10.0.0.112/api/cluster/autotune \
  -d '{"action":"disableWatchdog"}'

# Check status
curl http://10.0.0.112/api/cluster/autotune/status | jq '.watchdogEnabled, .watchdogRunning'
```

---

### Files Changed (Watchdog Feature)

| File | Changes |
|------|---------|
| `cluster_autotune.c` | Watchdog task, step-down helpers, state tracking |
| `cluster_autotune.h` | Watchdog API function declarations |
| `http_server.c` | Watchdog status in GET, enable/disable actions in POST |
| `cluster.component.ts` | Watchdog state, toggle method |
| `cluster.component.html` | InputSwitch toggle with shield icon |
| `cluster.service.ts` | Interface additions, setWatchdog method |

---

### 23. Autotune UI Redesign

**Problem:** The autotune section was cramped, dropdown too narrow to read, and located below the Master device card making it unclear it was a global function.

**Solution:** Complete redesign of the Cluster Auto-Tune section.

**Changes:**

1. **Moved above Master device** - Now clearly positioned as a global cluster control

2. **3-Column Grid Layout:**
   - **Left (col-3):** Oscilloscope visualization (responsive width, taller)
   - **Center (col-6):** Mode dropdown + action buttons + live status panel
   - **Right (col-3):** Best results card

3. **Wider Dropdown:** Changed from `w-12rem` (192px) to `w-20rem` (320px)

4. **Visual Indicators When Running:**
   - Cyan border around entire section (`border-2 border-cyan-500`)
   - Spinning cog icon in header
   - "Tuning: Master" or "Tuning: Slave X" label
   - Large progress percentage display
   - Frequency/voltage/test count with colored icons

5. **Visual Indicators When Locked:**
   - Green border around section (`border-2 border-green-500`)
   - "Settings Locked" badge in results panel

6. **Watchdog Toggle:** Moved to top-right corner in highlighted pill container

7. **Results Panel:** Shows best freq/voltage/efficiency or "No results yet" placeholder

**New HTML Structure:**
```html
<div class="surface-card border-round p-3 mb-3"
     [ngClass]="{'border-2 border-cyan-500': autotuneStatus?.running,
                 'border-2 border-green-500': autotuneStatus?.stateCode === 5}">
    <!-- Header with title + watchdog toggle -->
    <!-- 3-column grid: oscilloscope | controls | results -->
</div>
```

---

### 24. Watchdog Toggle Bug Fix

**Problem:** Clicking the watchdog toggle always showed "Watchdog disabled" message regardless of toggle direction.

**Root Cause:** The `[(ngModel)]` two-way binding updates `watchdogEnabled` BEFORE the `(onChange)` handler runs. So when `toggleWatchdog()` calculated `newState = !this.watchdogEnabled`, it was negating the already-updated value, effectively reversing the user's action.

**Fix:**
```typescript
// BEFORE (broken):
toggleWatchdog(): void {
    const newState = !this.watchdogEnabled;  // Wrong! Already flipped by ngModel
    ...
}

// AFTER (fixed):
toggleWatchdog(): void {
    const newState = this.watchdogEnabled;  // Correct! Use current value
    ...
    error: () => {
        this.watchdogEnabled = !newState;  // Revert on error
    }
}
```

---

### 25. Compilation Fix: cluster_slave_t Field Name

**Error:** `'cluster_slave_t' has no member named 'voltage'; did you mean 'voltage_in'?`

**Fix:** Changed `slave_info.voltage` to `slave_info.core_voltage` in watchdog task (line 1289).

---

### Files Changed (UI Redesign Session)

| File | Changes |
|------|---------|
| `cluster.component.html` | Complete autotune section redesign, moved above Master |
| `cluster.component.ts` | Fixed watchdog toggle logic |
| `cluster.service.ts` | Added `currentDevice` to IAutotuneStatus interface |
| `cluster_autotune.c` | Fixed `core_voltage` field name |

---

### 26. Button Styling Fix - White Text on Red Buttons

**Problem:** Danger buttons (Restart All, Restart Slave) had red theme but text/icons weren't white - hard to read.

**Root Cause:** Buttons used `p-button-danger p-button-outlined` which creates transparent background with red text/border.

**Fix:** Removed `p-button-outlined` from danger buttons to make them solid red with white text:

```html
<!-- BEFORE -->
<button pButton icon="pi pi-refresh" class="p-button-sm p-button-danger p-button-outlined p-button-rounded">

<!-- AFTER -->
<button pButton icon="pi pi-refresh" class="p-button-sm p-button-danger p-button-rounded">
```

**Buttons fixed:**
- Restart All (bulk action header)
- Restart Slave (slave expanded panel)

---

### 27. Slave Device Info Now Fetches Real Data

**Problem:** Slave Device Info panel showed placeholder values:
- Model: "Bitaxe"
- Firmware: "2.x"
- Uptime: 0s
- Free Heap: 0 bytes

**Root Cause:** The `/api/cluster/slave/{id}/config` endpoint returned hardcoded values because `cluster_slave_t` struct only contains runtime stats, not device info.

**Solution:** Now proxies to slave's `/api/system` endpoint to fetch real device info.

**Implementation in `http_server.c`:**

```c
// Try to fetch real device info from slave's /api/system if it has an IP
char *slave_response = NULL;
cJSON *slave_system = NULL;
if (strlen(slave_info.ip_addr) > 0) {
    esp_err_t err = http_proxy_to_slave(slave_info.ip_addr, "/api/system",
                                         HTTP_METHOD_GET, NULL, &slave_response);
    if (err == ESP_OK && slave_response) {
        slave_system = cJSON_Parse(slave_response);
        free(slave_response);
    }
}

if (slave_system) {
    // Extract real values: hostname, deviceModel, version, uptimeSeconds,
    // freeHeap, frequency, coreVoltageActual, fanspeed, autofanspeed,
    // targetTemp, hashRate, power, temp
} else {
    // Fallback for ESP-NOW only slaves (no IP)
    // Uses cluster status data with "Bitaxe (ESP-NOW)" as model
}
```

**Field Mappings (slave /api/system → frontend):**
| Slave API Field | Frontend Field |
|-----------------|----------------|
| hostname | hostname |
| deviceModel | deviceModel |
| version | fwVersion |
| uptimeSeconds | uptime |
| freeHeap | freeHeap |
| frequency | frequency |
| coreVoltageActual / coreVoltage | coreVoltage |
| fanspeed | fanSpeed |
| autofanspeed | fanMode (inverted) |
| targetTemp / autofantemp | targetTemp |
| hashRate | hashrate |
| power | power |
| temp | chipTemp |

**Behavior:**
- Slaves with IP address: Full device info fetched from slave
- ESP-NOW only slaves: Shows "Bitaxe (ESP-NOW)" with limited data from cluster status

---

### Files Changed (Button & Device Info Session)

| File | Changes |
|------|---------|
| `cluster.component.html` | Removed `p-button-outlined` from danger buttons |
| `http_server.c` | Slave config now proxies to `/api/system` for real device info |

---

### PENDING

- [ ] Build firmware (`idf.py build`)
- [ ] Flash to master device
- [ ] Test slave autotune end-to-end
- [ ] Verify HTTP PATCH reaches slaves correctly
- [ ] Test device selection (master only, slaves only, specific slaves)
- [ ] Test watchdog triggers on high temp
- [ ] Test watchdog triggers on low Vin
- [ ] Test new autotune UI layout
- [ ] Test slave device info loading with real data

---

## Session Continuation: December 31, 2025

---

### 28. Fixed Watchdog Monitoring - Always Active

**Problem:** User wanted the watchdog to always monitor temp and vin, not skip during autotune.

**Solution:** Updated watchdog to always monitor and stop autotune if safety limits are exceeded.

**Changes to `cluster_autotune.c`:**
- Removed "skip during autotune" check from watchdog task
- Added logic to call `cluster_autotune_stop(false)` if watchdog triggers during autotune
- Added 60-second cooldown for both master and slaves after watchdog activation
- Thresholds: temp > 65°C, vin < 4.9V

---

### 29. Fixed Slave Settings Not Applying

**Problem:** When manually applying frequency/voltage to slaves via UI, the settings didn't actually apply - the endpoint just logged and returned success.

**Solution:** Implemented actual HTTP PATCH proxy to slave's `/api/system`.

**Changes to `http_server.c`:**
```c
// POST /api/cluster/slave/{id}/setting
case 0x20:  // FREQUENCY
    snprintf(patch_data, sizeof(patch_data), "{\"frequency\":%d}", val);
    break;
case 0x21:  // CORE_VOLTAGE
    snprintf(patch_data, sizeof(patch_data), "{\"coreVoltage\":%d}", val);
    break;
// ... other settings

esp_err_t err = http_proxy_to_slave(slave_info.ip_addr, "/api/system",
                                     HTTP_METHOD_PATCH, patch_data, &response);
```

---

### 30. Fixed Slave Device Info Not Loading

**Problem:** Slave dropdown showed "Unknown" for firmware, 0s uptime, etc.

**Root Causes & Fixes:**

1. **HTTP event handler only captured non-chunked responses** - Fixed to handle both chunked and non-chunked by using realloc to accumulate data:
```c
case HTTP_EVENT_ON_DATA:
    if (evt->data_len > 0) {
        char *new_buf = realloc(http_proxy_response,
                                http_proxy_response_len + evt->data_len + 1);
        memcpy(http_proxy_response + http_proxy_response_len, evt->data, evt->data_len);
        http_proxy_response_len += evt->data_len;
    }
```

2. **Wrong endpoint** - Was using `/api/system` instead of `/api/system/info`. Fixed:
```c
esp_err_t err = http_proxy_to_slave(slave_info.ip_addr, "/api/system/info",
                                     HTTP_METHOD_GET, NULL, &slave_response);
```

---

### 31. Fixed Master Settings Dropdown Refreshing

**Problem:** Master settings dropdown kept refreshing every 3 seconds, resetting input values before user could apply them.

**Root Cause:** `loadMasterInfo()` was called every 3 seconds on the polling interval and overwrote the input values.

**Solution:** Only initialize edit values once when panel is expanded:

```typescript
// Added flag to track initialization
private masterEditValuesInitialized: boolean = false;

loadMasterInfo(): void {
    this.systemService.getInfo('').subscribe({
        next: (info) => {
            this.masterInfo = info;
            // Only initialize editable values once
            if (!this.masterEditValuesInitialized) {
                this.masterFrequency = info.frequency || 500;
                this.masterVoltage = info.coreVoltage || 1200;
                // ... other values
                this.masterEditValuesInitialized = true;
            }
        }
    });
}

toggleMasterExpanded(): void {
    this.masterExpanded = !this.masterExpanded;
    if (this.masterExpanded) {
        // Reset flag so values are refreshed when panel opens
        this.masterEditValuesInitialized = false;
        this.loadMasterInfo();
    }
}
```

---

### Files Changed (December 31, 2025)

| File | Changes |
|------|---------|
| `cluster_autotune.c` | Watchdog always monitors, stops autotune on trigger, 60s cooldown |
| `cluster.component.ts` | Master edit values only init once, flag resets on panel toggle |
| `cluster.service.ts` | Added proxyDebug to ISlaveConfig interface |
| `http_server.c` | Slave settings actually apply via HTTP PATCH, chunked response fix, correct endpoint |

---

### Git Commit (December 31, 2025)

```
Fix slave settings, device info, and master dropdown refresh

- Watchdog now always monitors temp/vin, stops autotune if triggered
- Slave settings now actually apply via HTTP PATCH to /api/system
- Fixed slave device info not loading (chunked responses, wrong endpoint)
- Master settings inputs no longer reset during polling
```

---

## Session: January 1, 2026

---

### 32. Pool Hashrate Discrepancy - BM1370 Timing Issue

**Problem:** After rebuilding firmware, pool/stratum reports ~40% of actual hashrate.
- UI shows correct total: 5.91 TH/s (master + slaves combined)
- Pool shows: 2.32 TH/s
- Machine reports: 117 unknown rejections (0.75%)

**Root Cause:** BM1370 ASIC job interval was reset to default **500ms** during rebuild.
Previously optimized to **700ms** which was the "sweet spot" found through testing.

**Fixes Applied:**

1. **`components/asic/asic.c`** - Changed BM1370 timing from 500ms to 700ms:
```c
case BM1368:
    return 500 / GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;
case BM1370:
    return 700 / GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;  // Optimized for BM1370
```

2. **`cluster_integration.c`** - Increased MAX_JOB_MAPPINGS from 128 to 256 to prevent job mapping loss with multiple slaves

3. Added better error logging for job mapping failures (ESP_LOGE instead of ESP_LOGW)

---

### 33. LOST FEATURE: Auto-Timing / Self-Adjusting Job Interval

**Status:** NEEDS TO BE RECREATED

**What was lost:** An auto-timing/self-adjusting feature that would dynamically adjust the ASIC job interval to find the optimal timing for the current network/pool latency.

**What we know from user:**
- Found optimal timing at ~700ms for BM1370 through manual testing
- Had an option to have it "self time" to adjust for latency automatically
- Would find the sweet spot without manual intervention

**Questions to answer when recreating:**
1. What did it adjust? (ASIC job interval - the value returned by `ASIC_get_asic_job_frequency_ms()`)
2. What did it measure? (Share acceptance rate? Rejection rate? Unknown rejections? Pool latency?)
3. How did it adjust? (Calibration at startup? Runtime adjustment based on rejection rate?)
4. Where was the setting? (UI toggle? Kconfig option? Always-on?)

**Possible implementation approaches:**

1. **Startup Calibration:**
   - At boot, test different intervals (500, 600, 700, 800ms)
   - Monitor share acceptance/rejection for each
   - Lock in the best one

2. **Runtime Adjustment:**
   - Monitor "unknown" rejection rate continuously
   - If rejections increase, adjust interval up/down
   - Use moving average to avoid oscillation

3. **Latency-Based:**
   - Measure pool response latency
   - Calculate optimal interval based on RTT
   - Adjust automatically as latency changes

4. **Hybrid:**
   - Calibrate at startup to find baseline
   - Fine-tune during runtime based on rejection rate

**Target metrics:**
- Minimize "unknown" rejections
- Maximize pool-reported hashrate vs local hashrate ratio
- Optimal range for BM1370 appears to be 600-800ms (700ms sweet spot)

**Files that would need changes:**
- `components/asic/asic.c` - Make job interval dynamic instead of hardcoded
- `main/global_state.h` - Add field for current job interval
- `main/nvs_config.h/c` - Persist optimal interval to NVS
- `main/http_server/http_server.c` - API to get/set interval, enable/disable auto-timing
- `cluster.component.ts/html` - UI toggle for auto-timing feature

---

### Files Changed (January 1, 2026)

| File | Changes |
|------|---------|
| `components/asic/asic.c` | BM1370 timing: 500ms → 700ms |
| `cluster_integration.c` | MAX_JOB_MAPPINGS: 128 → 256, better error logging |

---

### Pending Tasks

- [ ] Rebuild firmware with 700ms timing fix
- [ ] Test that pool hashrate matches UI after fix
- [ ] Recreate auto-timing feature (needs user input on original behavior)
- [ ] Consider adding job interval to Kconfig for easy adjustment
- [ ] Consider adding UI control for manual job interval override

---

## Session Continuation: January 1, 2026 (Part 2)

### Work Rebroadcast Timing Experiments

**Problem:** After rebuilding firmware, pool hashrate was ~40% of actual (2.37 TH/s vs 6 TH/s reported).

**Root Cause Identified:** `WORK_REBROADCAST_INTERVAL_MS` was 10 seconds - way too long. Slaves were exhausting their nonce space and finding duplicate shares.

**Experiments:**

| Interval | Unknown Rejections | Notes |
|----------|-------------------|-------|
| 10000ms (original) | 0.75% | Pool hashrate ~40% of actual |
| 700ms | 12.07% | Too aggressive, merkle root sync issues |
| 1500ms | Still high | Testing... |

**Key Insight:**
- Too slow (10s): Slaves exhaust nonce space, duplicates filtered, low pool hashrate
- Too fast (700ms): Work changes before shares submitted, merkle mismatch rejections

**Current Status:**
- Master: `WORK_REBROADCAST_INTERVAL_MS = 1500` in `cluster_master.c:632`
- Slave ASIC timing: 700ms in `asic.c:131`
- Need to find sweet spot that balances both issues

**Hypothesis:**
The slave ASIC timing (700ms) might need to match or coordinate with the work rebroadcast interval. If slaves are internally cycling jobs at 700ms but getting new work every 1500ms, there could be timing conflicts.

**Files Changed:**
| File | Changes |
|------|---------|
| `cluster_master.c:632` | WORK_REBROADCAST_INTERVAL_MS: 10000 → 700 → 1500 → 700 |
| `cluster_autotune.c:1324` | Added #if CLUSTER_IS_MASTER guard for watchdog_slave_last_action |

---

### BUG FIX: Job → Extranonce2 Race Condition

**Root Cause Found:**
When a share was found, the slave copied extranonce2 from `current_work`. But if master had sent new work (with new extranonce2) while the ASIC was still working on the old job, the share would be submitted with the WRONG extranonce2.

**The Race Condition:**
1. Master sends work with `extranonce2=A`, slave stores it
2. Slave submits job to ASIC
3. Master sends new work with `extranonce2=B`, slave updates `current_work`
4. ASIC finds share for OLD job (which used `en2=A`)
5. Slave copies `extranonce2` from `current_work` which is now **B** (wrong!)
6. Share submitted to pool with wrong extranonce2 → merkle mismatch → "low difficulty" rejection

**The Fix (cluster_slave.c):**
1. Added `job_en2_mapping_t` structure to store job_id → extranonce2 mappings
2. Added `store_job_mapping()` - called when work is submitted to ASIC
3. Added `lookup_job_mapping()` - called when share is found
4. Modified `cluster_slave_on_share_found()` to look up the correct extranonce2 instead of using current_work

**Code Added:**
```c
#define MAX_JOB_MAPPINGS 16  // Circular buffer of recent jobs

typedef struct {
    uint32_t job_id;
    uint8_t extranonce2[8];
    uint8_t extranonce2_len;
    bool valid;
} job_en2_mapping_t;

static job_en2_mapping_t g_job_mappings[MAX_JOB_MAPPINGS] = {0};
```

**Files Changed:**
| File | Changes |
|------|---------|
| `cluster_slave.c` | Added job→en2 mapping, store on work submit, lookup on share found |

---

### BUG FIX ITERATION 2: Pass Extranonce2 from ASIC Job Directly

**Problem with First Fix:**
The job mapping approach failed because the same `job_id` can have MULTIPLE different `extranonce2` values over time. When a share was found, the lookup found the FIRST (oldest, stale) mapping for that job_id, not the correct one.

**Log Evidence:**
```
Share en2=01000034 but work has en2=01000042
Share en2=01000034 but work has en2=01000043
Share en2=01000034 but work has en2=01000045
```

**The Real Fix:**
The ASIC job structure (`active_job`) already has the correct extranonce2 that was used when that specific job was submitted. Pass it directly through the call chain instead of any lookup.

**Changes Made:**

1. **`cluster.h:376`** - Updated function signature:
```c
void cluster_slave_on_share_found(uint32_t nonce, uint32_t job_id, uint32_t version,
                                   uint32_t ntime, const char *extranonce2_hex);
```

2. **`cluster_integration.c:673-682`** - Pass job's extranonce2:
```c
// Get the extranonce2 from the job struct - this is the CORRECT one
const char *job_en2 = active_job->extranonce2 ? active_job->extranonce2 : "";
cluster_slave_on_share_found(nonce, numeric_job_id, version, ntime, job_en2);
```

3. **`cluster_slave.c`** - Use passed extranonce2:
```c
void cluster_slave_on_share_found(uint32_t nonce, uint32_t job_id, uint32_t version,
                                   uint32_t ntime, const char *extranonce2_hex)
{
    // Use the extranonce2 from the ASIC job (passed in from intercept_share)
    if (extranonce2_hex && extranonce2_hex[0]) {
        size_t hex_len = strlen(extranonce2_hex);
        share.extranonce2_len = hex_len / 2;
        if (share.extranonce2_len > 8) share.extranonce2_len = 8;

        for (int i = 0; i < share.extranonce2_len; i++) {
            char byte_str[3] = {extranonce2_hex[i*2], extranonce2_hex[i*2+1], '\0'};
            share.extranonce2[i] = (uint8_t)strtol(byte_str, NULL, 16);
        }
    }
}
```

**Files Changed:**
| File | Changes |
|------|---------|
| `cluster.h` | Added `extranonce2_hex` param to `cluster_slave_on_share_found()` |
| `cluster_integration.c` | Pass `active_job->extranonce2` to share handler |
| `cluster_slave.c` | Use passed extranonce2 instead of lookup |

**Status:** Code changes complete, needs rebuild and test.

---
