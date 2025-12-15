# DEFENSIVE PUBLICATION

## Clusteraxe: Coordinated Multi-Device Bitcoin Mining System Using BAP and ESP-NOW Protocols

**Technical Disclosure Document**

---

| Field | Value |
|-------|-------|
| **Publication Date** | December 15, 2025 |
| **Inventor** | Shae (digitalnom@gmail.com) |
| **Field** | Cryptocurrency Mining Hardware/Firmware, Distributed Systems, ESP32 Communication Protocols |
| **Status** | 90% Complete — Active Development |
| **Document ID** | CLUSTERAXE-DP-2025-001 |

---

## ABSTRACT

This disclosure describes **Clusteraxe**, an open-source firmware system that enables multiple standalone Bitaxe cryptocurrency mining devices to operate as a coordinated cluster. The system introduces novel methods for device-to-device communication using the Bitaxe Accessory Port (BAP) serial interface and/or ESP-NOW wireless protocol, implementing coordinated nonce range partitioning, unified work distribution, thermal load balancing, and aggregated hashrate reporting.

This publication establishes prior art to prevent third-party patent claims on these specific implementations while preserving the technology for open-source community use.

---

## 1. BACKGROUND AND PROBLEM STATEMENT

### 1.1 Current State of Bitaxe Ecosystem

The Bitaxe is an open-source Bitcoin ASIC miner developed by Open Source Miners United (OSMU). As of December 2025, over 50,000 Bitaxe units are deployed globally. Each device operates independently, connecting to mining pools via WiFi using the Stratum protocol. The existing AxeOS firmware provides a "Swarm" feature for monitoring multiple devices from a single interface, but each device maintains independent pool connections and performs uncoordinated mining work.

### 1.2 Technical Limitations Addressed

1. **Duplicate Work:** Multiple Bitaxes mining to the same pool may unknowingly hash identical nonce ranges, wasting computational resources.

2. **Network Overhead:** Each device maintains separate Stratum connections, multiplying bandwidth and latency.

3. **Inefficient Load Distribution:** No mechanism exists to balance work based on individual device thermal or performance states.

4. **Fragmented Statistics:** Users with multiple devices cannot view aggregated performance without external tools.

---

## 2. DETAILED TECHNICAL DESCRIPTION

### 2.1 System Architecture Overview

Clusteraxe implements a **coordinator-worker architecture** where one Bitaxe device serves as the "Coordinator Node" and remaining devices function as "Worker Nodes." The coordinator maintains the single Stratum pool connection, receives mining jobs, partitions work, and distributes assignments to workers via BAP or ESP-NOW.

#### Node Roles

- **Coordinator Node:** Manages pool connection, job distribution, result aggregation, and cluster health monitoring. Implements the Stratum client and acts as a local Stratum proxy for workers.

- **Worker Node:** Receives partitioned work assignments, executes SHA256d hashing on assigned nonce ranges, reports valid shares and telemetry to coordinator.

### 2.2 BAP-Based Wired Communication Method

The Bitaxe Accessory Port (BAP) provides a 115200 baud serial interface using NMEA-style ASCII sentences. Clusteraxe extends this protocol for inter-device clustering.

#### 2.2.1 Physical Layer Configuration

1. **Direct UART Connection:** For 2-device clusters, direct TX/RX crossover between BAP ports.

2. **RS-485 Bus Topology:** For 3+ device clusters, RS-485 transceivers enable multi-drop half-duplex communication over twisted pair wiring up to 1200 meters.

3. **Daisy-Chain Serial:** Alternative topology where each device forwards messages to the next in chain.

#### 2.2.2 Clusteraxe Protocol Extensions (NMEA-Style)

Novel message types introduced:

| Message | Purpose |
|---------|---------|
| `$CXJOB` | Work distribution message containing job_id, block header template, target difficulty, and assigned nonce_start/nonce_end range |
| `$CXSHR` | Share submission from worker containing job_id, nonce, extranonce2, and timestamp |
| `$CXTEL` | Telemetry broadcast containing device_id, hashrate, temperature, fan_speed, voltage, and error_count |
| `$CXSYN` | Clock synchronization message for coordinated job timing |
| `$CXCFG` | Configuration propagation for cluster-wide settings |

### 2.3 ESP-NOW Wireless Communication Method

ESP-NOW is Espressif's peer-to-peer wireless protocol operating at the data-link layer, enabling sub-millisecond latency communication without WiFi router dependency. Clusteraxe implements ESP-NOW as an alternative or complement to BAP wiring.

#### 2.3.1 Network Topology

- **Star Topology:** Coordinator communicates directly with all workers (1-to-many).
- **Mesh Topology:** Workers can relay messages, extending range beyond single-hop ESP-NOW limits (~200m open air).
- **Hybrid Mode:** ESP-NOW for control plane, WiFi maintained for fallback pool connection.

#### 2.3.2 Coexistence with WiFi

Clusteraxe implements channel-locked ESP-NOW operation where all cluster devices operate on the same WiFi channel as the coordinator's pool connection. Worker nodes disable their independent WiFi Stratum connections when joining a cluster, reducing network congestion.

### 2.4 Nonce Range Partitioning Algorithm

The coordinator implements adaptive nonce space partitioning that accounts for heterogeneous device capabilities:

**Algorithm:**

1. Query each worker's reported hashrate and thermal headroom.

2. Calculate proportional nonce allocation:
   ```
   device_range = (device_hashrate / cluster_hashrate) × total_nonce_space
   ```

3. Apply thermal throttling factor:
   ```
   if device_temp > threshold:
       allocation = allocation × (1 - (temp - threshold) × penalty_factor)
   ```

4. Distribute non-overlapping ranges with guard bands to prevent edge-case collisions.

### 2.5 Aggregated Statistics and Monitoring

The coordinator node hosts an extended AxeOS web interface displaying:

- Combined cluster hashrate
- Per-device contribution breakdown
- Thermal distribution heatmap
- Network latency metrics
- Share acceptance rate by device
- Projected time-to-block calculations based on aggregate hashrate

---

## 3. NOVEL CONTRIBUTIONS (PRIOR ART CLAIMS)

This publication establishes prior art for the following specific implementations:

- ✓ A method for coordinating multiple standalone Bitaxe ASIC mining devices using the BAP serial interface with extended NMEA-style protocol messages for work distribution and result aggregation.

- ✓ A method for coordinating multiple Bitaxe devices using ESP-NOW wireless protocol for sub-millisecond job distribution without requiring WiFi infrastructure.

- ✓ A nonce range partitioning algorithm that dynamically allocates work based on real-time device hashrate and thermal telemetry.

- ✓ A coordinator-worker architecture where a single Bitaxe maintains pool connectivity and acts as a local Stratum proxy for clustered worker devices.

- ✓ A hybrid communication system combining wired BAP and wireless ESP-NOW for redundant cluster connectivity.

- ✓ An aggregated monitoring interface displaying unified statistics across a cluster of Bitaxe mining devices.

---

## 4. DISTINCTION FROM EXISTING SOLUTIONS

### 4.1 Existing Solutions Reviewed

| Solution | Limitation |
|----------|------------|
| **AxeOS Swarm** | Management-only interface; each device mines independently with separate pool connections |
| **Pogolo/Bitaxe-Pool** | Local Stratum servers that devices connect to; no device-to-device coordination or nonce partitioning |
| **ESP-Miner-Multichip** | Multi-ASIC support on a single PCB; not network-based clustering of separate devices |
| **BAP Protocol (v2.10.0)** | Accessory communication only; no defined messages for inter-miner coordination |

### 4.2 Key Differentiators

Clusteraxe is the first implementation to:

1. Use BAP for device-to-device mining coordination rather than accessories
2. Apply ESP-NOW for cryptocurrency miner clustering
3. Implement dynamic nonce partitioning across networked Bitaxe devices
4. Provide true work coordination rather than mere monitoring aggregation

---

## 5. LEGAL NOTICE AND INTENDED USE

This defensive publication is made available to establish prior art and prevent future patent claims by third parties on the described technologies.

The inventor retains all rights to develop, distribute, and commercialize implementations of this technology. This disclosure does not constitute a patent application and explicitly places the described methods into the public domain **for the purpose of prior art establishment only**.

Commercial rights to the Clusteraxe brand, software implementation, and associated services remain with the inventor.

---

## APPENDIX: TECHNICAL SPECIFICATIONS

| Parameter | Specification |
|-----------|---------------|
| BAP Baud Rate | 115200 (default), configurable to 230400 |
| ESP-NOW Data Rate | 1 Mbps (802.11 LR mode for extended range) |
| Max Cluster Size | 20 devices (ESP-NOW peer limit), expandable via mesh |
| Job Distribution Latency | <5ms (ESP-NOW), <10ms (BAP serial) |
| Target Platform | ESP32-S3-WROOM-1 (Bitaxe 400/600 series) |
| Firmware Base | ESP-IDF v5.x, forked from ESP-Miner |

---

**— END OF DEFENSIVE PUBLICATION —**

*Document ID: CLUSTERAXE-DP-2025-001*

*Timestamp: December 15, 2025*

---

### Publication Locations

For prior art validity, this document should be archived at:

- [ ] Internet Archive (archive.org)
- [ ] GitHub public repository
- [ ] Research disclosure databases (IP.com, ResearchDisclosure.com)
- [ ] Timestamped blockchain publication (optional)
