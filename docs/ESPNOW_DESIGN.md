# ESP-NOW Transport Layer Design

## Overview

This document describes the design for adding ESP-NOW as an alternative transport layer to ClusterAxe, enabling wireless cluster communication without physical BAP cables.

## Architecture

### Transport Abstraction Layer

```
┌─────────────────────────────────────────────────────────────┐
│                    Cluster Core Logic                        │
│     (cluster.c, cluster_master.c, cluster_slave.c)          │
└─────────────────────────────┬───────────────────────────────┘
                              │
                   cluster_transport.h (API)
                              │
              ┌───────────────┴───────────────┐
              │                               │
    ┌─────────▼─────────┐          ┌─────────▼─────────┐
    │  cluster_bap.c    │          │ cluster_espnow.c  │
    │  (UART/BAP)       │          │ (Wireless)        │
    └───────────────────┘          └───────────────────┘
```

### Transport Selection (Kconfig)

```
CONFIG_CLUSTER_TRANSPORT_BAP=y      # UART/BAP cable (current)
CONFIG_CLUSTER_TRANSPORT_ESPNOW=y   # ESP-NOW wireless
CONFIG_CLUSTER_TRANSPORT_BOTH=y     # Both (fallback capable)
```

---

## ESP-NOW Specifics

### Constraints & Capabilities

| Parameter | Value |
|-----------|-------|
| Max Peers | 20 (17 encrypted) |
| Max Payload (v1.0) | 250 bytes |
| Max Payload (v2.0) | 1470 bytes |
| Latency | 1-5ms typical |
| Range | ~200m (open), ~50m (indoor) |
| Encryption | AES-CCMP (optional) |
| Channel | Must match WiFi channel |

### Peer Discovery Protocol

#### Master Broadcast Discovery

```
1. Master starts in "discovery mode"
2. Master broadcasts discovery beacon every 1 second:
   - Message: $CLDSC,master_mac,cluster_id,channel*XX
3. Slaves listen for discovery beacons
4. Slave sends registration request to master MAC
5. Master adds slave as ESP-NOW peer
6. Master sends ACK with assigned slave_id
7. Normal cluster operation begins
```

#### Message Flow

```
MASTER                                          SLAVE
   │                                               │
   │──── $CLDSC (broadcast) ──────────────────────>│
   │                                               │
   │<─── $CLREG,hostname,mac_addr ─────────────────│
   │                                               │
   │──── $CLACK,slave_id ─────────────────────────>│
   │                                               │
   │──── $CLWRK,job_data... ──────────────────────>│
   │                                               │
   │<─── $CLHBT,slave_id,stats... ─────────────────│
   │                                               │
   │<─── $CLSHR,slave_id,share_data... ────────────│
   │                                               │
```

---

## Data Structures

### ESP-NOW Peer Entry

```c
typedef struct {
    uint8_t         mac_addr[6];        // Peer MAC address
    uint8_t         slave_id;           // Assigned slave ID (0-15)
    bool            encrypted;          // Use encryption
    uint8_t         lmk[16];            // Local Master Key (if encrypted)
    int64_t         last_seen;          // Last packet timestamp
    int8_t          rssi;               // Signal strength
} cluster_espnow_peer_t;
```

### ESP-NOW State

```c
typedef struct {
    bool                    initialized;
    uint8_t                 self_mac[6];        // Our MAC address
    uint8_t                 channel;            // WiFi channel
    char                    cluster_id[16];     // Cluster identifier
    bool                    discovery_active;   // Broadcasting discovery
    cluster_espnow_peer_t   peers[CLUSTER_MAX_SLAVES];
    uint8_t                 peer_count;
    SemaphoreHandle_t       peers_mutex;
    QueueHandle_t           rx_queue;           // Received messages
    TaskHandle_t            rx_task;            // Message processor
} cluster_espnow_state_t;
```

---

## Transport API (cluster_transport.h)

```c
/**
 * Transport-agnostic API for cluster communication
 */

typedef enum {
    CLUSTER_TRANSPORT_NONE = 0,
    CLUSTER_TRANSPORT_BAP,
    CLUSTER_TRANSPORT_ESPNOW
} cluster_transport_type_t;

/**
 * Initialize the transport layer
 */
esp_err_t cluster_transport_init(cluster_transport_type_t type);

/**
 * Deinitialize transport
 */
void cluster_transport_deinit(void);

/**
 * Send message to specific slave (master -> slave)
 * @param slave_id Target slave ID (0xFF for broadcast)
 * @param data Message data
 * @param len Message length
 */
esp_err_t cluster_transport_send(uint8_t slave_id, const uint8_t *data, size_t len);

/**
 * Send message to master (slave -> master)
 * @param data Message data
 * @param len Message length
 */
esp_err_t cluster_transport_send_to_master(const uint8_t *data, size_t len);

/**
 * Register receive callback
 */
typedef void (*cluster_transport_rx_cb_t)(const uint8_t *data, size_t len, void *ctx);
esp_err_t cluster_transport_register_rx_callback(cluster_transport_rx_cb_t cb, void *ctx);

/**
 * Start peer discovery (master mode)
 */
esp_err_t cluster_transport_start_discovery(void);

/**
 * Stop peer discovery
 */
void cluster_transport_stop_discovery(void);

/**
 * Get transport type
 */
cluster_transport_type_t cluster_transport_get_type(void);

/**
 * Get peer signal strength (ESP-NOW only)
 * @param slave_id Slave ID
 * @return RSSI in dBm, or 0 if not available
 */
int8_t cluster_transport_get_rssi(uint8_t slave_id);
```

---

## Implementation Files

### New Files

| File | Purpose |
|------|---------|
| `cluster_transport.h` | Transport abstraction API |
| `cluster_transport.c` | Transport router/dispatcher |
| `cluster_espnow.h` | ESP-NOW specific definitions |
| `cluster_espnow.c` | ESP-NOW implementation |

### Modified Files

| File | Changes |
|------|---------|
| `cluster.c` | Use transport API instead of direct BAP calls |
| `cluster_master.c` | Use transport API, add discovery logic |
| `cluster_slave.c` | Use transport API, add discovery listener |
| `Kconfig.projbuild` | Add transport selection options |

---

## Kconfig Options

```kconfig
menu "Cluster Transport Configuration"

choice CLUSTER_TRANSPORT
    prompt "Cluster Transport Layer"
    default CLUSTER_TRANSPORT_BAP
    depends on CLUSTER_ENABLED

config CLUSTER_TRANSPORT_BAP
    bool "BAP (UART Cable)"
    help
        Use BAP protocol over UART for cluster communication.
        Requires physical cable connection between devices.

config CLUSTER_TRANSPORT_ESPNOW
    bool "ESP-NOW (Wireless)"
    help
        Use ESP-NOW for wireless cluster communication.
        No cables required, devices communicate via WiFi radio.

config CLUSTER_TRANSPORT_BOTH
    bool "Both (Auto-select)"
    help
        Support both transports. ESP-NOW preferred, BAP as fallback.

endchoice

config CLUSTER_ESPNOW_CHANNEL
    int "ESP-NOW Channel"
    default 1
    range 1 14
    depends on CLUSTER_TRANSPORT_ESPNOW || CLUSTER_TRANSPORT_BOTH
    help
        WiFi channel for ESP-NOW communication.
        Must match across all cluster devices.

config CLUSTER_ESPNOW_ENCRYPT
    bool "Enable ESP-NOW Encryption"
    default n
    depends on CLUSTER_TRANSPORT_ESPNOW || CLUSTER_TRANSPORT_BOTH
    help
        Enable AES-CCMP encryption for ESP-NOW packets.
        All devices must use the same encryption key.

config CLUSTER_ESPNOW_PMK
    string "ESP-NOW Primary Master Key"
    default "ClusterAxe_PMK!"
    depends on CLUSTER_ESPNOW_ENCRYPT
    help
        16-character primary master key for ESP-NOW encryption.

config CLUSTER_ESPNOW_DISCOVERY_INTERVAL_MS
    int "Discovery Beacon Interval (ms)"
    default 1000
    range 500 5000
    depends on CLUSTER_TRANSPORT_ESPNOW || CLUSTER_TRANSPORT_BOTH
    help
        How often master broadcasts discovery beacons.

endmenu
```

---

## ESP-NOW Implementation Details

### Initialization (cluster_espnow.c)

```c
esp_err_t cluster_espnow_init(void)
{
    // 1. Get WiFi interface MAC address
    esp_wifi_get_mac(WIFI_IF_STA, g_espnow_state.self_mac);

    // 2. Initialize ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());

    // 3. Register callbacks
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    // 4. Set PMK if encryption enabled
    #if CONFIG_CLUSTER_ESPNOW_ENCRYPT
    ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)CONFIG_CLUSTER_ESPNOW_PMK));
    #endif

    // 5. Create RX queue and task
    g_espnow_state.rx_queue = xQueueCreate(16, sizeof(espnow_rx_msg_t));
    xTaskCreate(espnow_rx_task, "espnow_rx", 4096, NULL, 5, &g_espnow_state.rx_task);

    // 6. Add broadcast peer for discovery
    esp_now_peer_info_t broadcast_peer = {
        .peer_addr = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
        .channel = CONFIG_CLUSTER_ESPNOW_CHANNEL,
        .ifidx = WIFI_IF_STA,
        .encrypt = false
    };
    ESP_ERROR_CHECK(esp_now_add_peer(&broadcast_peer));

    g_espnow_state.initialized = true;
    return ESP_OK;
}
```

### Discovery Beacon (Master)

```c
static void discovery_task(void *pvParameters)
{
    char beacon[64];

    while (g_espnow_state.discovery_active) {
        // Format: $CLDSC,mac,cluster_id,channel*XX
        snprintf(beacon, sizeof(beacon), "$CLDSC,%02X%02X%02X%02X%02X%02X,%s,%d",
                 g_espnow_state.self_mac[0], g_espnow_state.self_mac[1],
                 g_espnow_state.self_mac[2], g_espnow_state.self_mac[3],
                 g_espnow_state.self_mac[4], g_espnow_state.self_mac[5],
                 g_espnow_state.cluster_id,
                 CONFIG_CLUSTER_ESPNOW_CHANNEL);

        // Add checksum and send broadcast
        cluster_espnow_broadcast(beacon);

        vTaskDelay(pdMS_TO_TICKS(CONFIG_CLUSTER_ESPNOW_DISCOVERY_INTERVAL_MS));
    }

    vTaskDelete(NULL);
}
```

### Receive Callback

```c
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info,
                            const uint8_t *data,
                            int len)
{
    // Don't process in callback - queue for task
    espnow_rx_msg_t msg;
    memcpy(msg.mac_addr, recv_info->src_addr, 6);
    msg.data_len = len;
    memcpy(msg.data, data, len);
    msg.rssi = recv_info->rx_ctrl->rssi;

    xQueueSendFromISR(g_espnow_state.rx_queue, &msg, NULL);
}

static void espnow_rx_task(void *pvParameters)
{
    espnow_rx_msg_t msg;

    while (1) {
        if (xQueueReceive(g_espnow_state.rx_queue, &msg, portMAX_DELAY)) {
            // Route to cluster message handler
            cluster_on_transport_message_received(msg.data, msg.data_len, msg.mac_addr);
        }
    }
}
```

---

## Remote Slave Configuration

### The Problem

With BAP cables, each slave retains WiFi connectivity and its own web UI. Users can:
- Click slave IP in master dashboard → Opens slave web UI
- Adjust frequency, voltage, fan settings directly
- View logs, perform OTA updates

With ESP-NOW as primary transport, slaves may not have web access (especially if WiFi is dedicated to ESP-NOW channel). We need a way to manage slaves remotely from the master.

### Solution: Remote Configuration Protocol

New message types for remote configuration:

| Message | Direction | Purpose |
|---------|-----------|---------|
| `$CLGET` | Master → Slave | Get setting value |
| `$CLSET` | Master → Slave | Set setting value |
| `$CLCFG` | Master → Slave | Get full config snapshot |
| `$CLCMD` | Master → Slave | Execute command (restart, etc.) |
| `$CLCFR` | Slave → Master | Config response |
| `$CLSTR` | Slave → Master | Setting response |

### Configurable Settings

**Mining Settings (Read/Write):**
- Frequency (MHz)
- Core Voltage (mV)
- Fan Speed (%)
- Fan Mode (Auto/Manual)
- Target Temperature

**System Settings (Read-Only):**
- Hostname
- Device Model
- Firmware Version
- Uptime
- Hashrate, Power, Efficiency

### Master Web UI - Slave Configuration Panel

The master's cluster page will have an expandable panel for each slave:

```
┌─────────────────────────────────────────────────────────────┐
│ Slave #1: bitaxe-001                    [Active] [-45 dBm]  │
├─────────────────────────────────────────────────────────────┤
│ Hashrate: 425 GH/s    Temp: 52°C    Power: 12.3W           │
│                                                             │
│ [▼ Configure]                                               │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ Frequency:    [550] MHz    [Apply]                      │ │
│ │ Voltage:      [1200] mV    [Apply]                      │ │
│ │ Fan Speed:    [====●====] 65%  [Auto ▼]                 │ │
│ │ Target Temp:  [55] °C                                   │ │
│ │                                                         │ │
│ │ [Restart] [Identify] [View Logs]                        │ │
│ └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

### Master API Endpoints

```
GET /api/cluster/slave/{id}/config
    Returns full configuration snapshot for slave

GET /api/cluster/slave/{id}/setting/{setting_id}
    Returns specific setting value

POST /api/cluster/slave/{id}/setting
    Body: { "setting_id": 0x20, "value": 575 }
    Sets a setting on the slave

POST /api/cluster/slave/{id}/command
    Body: { "command": "restart" }
    Executes command on slave

POST /api/cluster/slaves/setting
    Body: { "setting_id": 0x20, "value": 550 }
    Sets setting on ALL slaves (bulk operation)
```

### Example: Change Frequency on All Slaves

```bash
# Set all slaves to 550 MHz
curl -X POST http://master-ip/api/cluster/slaves/setting \
  -H "Content-Type: application/json" \
  -d '{"setting_id": 32, "value": 550}'
```

## Web UI Changes

### New Settings Section

```html
<div class="cluster-transport">
    <h3>Transport Settings</h3>

    <div *ngIf="isEspNowAvailable">
        <label>Transport Mode</label>
        <p-dropdown [options]="transportOptions" [(ngModel)]="transportMode">
        </p-dropdown>
    </div>

    <div *ngIf="transportMode === 'espnow'">
        <label>WiFi Channel</label>
        <p-dropdown [options]="channelOptions" [(ngModel)]="espnowChannel">
        </p-dropdown>

        <label>Signal Strength</label>
        <div *ngFor="let slave of slaves">
            {{ slave.hostname }}: {{ slave.rssi }} dBm
            <p-progressBar [value]="rssiToPercent(slave.rssi)"></p-progressBar>
        </div>
    </div>
</div>
```

### API Extensions

```
GET /api/cluster/status
{
    ...
    "transport": "espnow",
    "espnow": {
        "channel": 1,
        "encrypted": false,
        "discoveryActive": true
    },
    "slaves": [
        {
            ...
            "rssi": -45,
            "macAddr": "AA:BB:CC:DD:EE:FF"
        }
    ]
}

POST /api/cluster/transport
{
    "type": "espnow",
    "channel": 6,
    "encrypt": true
}
```

---

## Migration Path

### Phase 1: Transport Abstraction
- Create `cluster_transport.h` API
- Wrap existing BAP code in `cluster_bap.c`
- Refactor cluster core to use transport API

### Phase 2: ESP-NOW Implementation
- Implement `cluster_espnow.c`
- Add discovery protocol
- Test single master + single slave

### Phase 3: Full Integration
- Add Kconfig options
- Update web UI
- Test with multiple slaves
- Performance comparison vs BAP

### Phase 4: Advanced Features
- Encryption support
- RSSI-based slave sorting
- Automatic channel selection
- Fallback from ESP-NOW to BAP

---

## Testing Plan

| Test | Description |
|------|-------------|
| Discovery | Master finds slave via broadcast |
| Registration | Slave registers with master |
| Work Distribution | Master sends work to all slaves |
| Share Submission | Slave submits share to master |
| Heartbeat | Regular status updates |
| Reconnection | Slave rejoins after disconnect |
| Range Test | Communication at various distances |
| Interference | Performance with other WiFi traffic |
| Multi-Slave | 4+ slaves simultaneously |
| Encryption | Verify encrypted communication |

---

## References

- [ESP-NOW ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html)
- [ESP-NOW GitHub Repository](https://github.com/espressif/esp-now)
- [Random Nerd Tutorials - ESP-NOW Guide](https://randomnerdtutorials.com/esp-now-esp32-arduino-ide/)
