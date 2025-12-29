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
