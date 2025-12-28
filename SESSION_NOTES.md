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
2. **Test autotune** - Verify mode limits work correctly
3. **Chart fine-tuning** - May need further adjustment based on real data
