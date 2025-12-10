# Clusteraxe Hardware Testing Checklist

## Pre-Testing Setup

### Hardware Required
- [ ] Bitaxe #1 (will be Master)
- [ ] Bitaxe #2 (will be Slave)
- [ ] BAP cable (TX/RX crossed, GND connected)
- [ ] USB cables for flashing
- [ ] Power supplies for both devices
- [ ] Computer with serial monitor (PuTTY, minicom, or ESP-IDF monitor)

### Firmware Files
- [ ] `clusteraxe-master.bin` - Master firmware
- [ ] `clusteraxe-slave.bin` - Slave firmware
- [ ] `bootloader.bin` - Bootloader
- [ ] `partition-table.bin` - Partition table
- [ ] `www.bin` - Web UI

---

## Test 1: Flash Master Device

### Steps
1. [ ] Connect Bitaxe #1 via USB
2. [ ] Put device in bootloader mode (hold BOOT, press RESET)
3. [ ] Flash master firmware:
   ```bash
   esptool.py --chip esp32s3 --port COM? --baud 460800 write_flash \
     -z --flash_mode dio --flash_freq 80m --flash_size detect \
     0x0 bootloader.bin \
     0x8000 partition-table.bin \
     0x10000 clusteraxe-master.bin \
     0x410000 www.bin
   ```
4. [ ] Reset device
5. [ ] Connect to WiFi AP or existing network

### Verify
- [ ] Device boots without errors
- [ ] Web UI accessible at device IP
- [ ] Navigate to Cluster page
- [ ] Mode shows "Master"
- [ ] Active Slaves shows 0

---

## Test 2: Flash Slave Device

### Steps
1. [ ] Connect Bitaxe #2 via USB
2. [ ] Put device in bootloader mode
3. [ ] Flash slave firmware:
   ```bash
   esptool.py --chip esp32s3 --port COM? --baud 460800 write_flash \
     -z --flash_mode dio --flash_freq 80m --flash_size detect \
     0x0 bootloader.bin \
     0x8000 partition-table.bin \
     0x10000 clusteraxe-slave.bin \
     0x410000 www.bin
   ```
4. [ ] Reset device
5. [ ] Connect to same WiFi network as master

### Verify
- [ ] Device boots without errors
- [ ] Web UI accessible at device IP
- [ ] Navigate to Cluster page
- [ ] Mode shows "Slave"
- [ ] Status shows "Waiting for master"

---

## Test 3: BAP Cable Connection

### Steps
1. [ ] Power off both devices
2. [ ] Connect BAP cable:
   - Master Pin 2 (TX) → Slave Pin 3 (RX)
   - Master Pin 3 (RX) → Slave Pin 2 (TX)
   - Master Pin 4 (GND) → Slave Pin 4 (GND)
3. [ ] Verify with multimeter:
   - [ ] No shorts between pins
   - [ ] TX-RX crossing is correct
   - [ ] GND-GND continuity
4. [ ] Power on Master first
5. [ ] Power on Slave

### Verify
- [ ] No smoke or burning smell
- [ ] Both devices power on normally
- [ ] Check serial monitor for any UART errors

---

## Test 4: Slave Registration

### Expected Behavior
1. Slave sends `$CLREG,hostname,ip_addr*XX`
2. Master responds with `$CLACK,slave_id*XX`
3. Slave stores assigned ID

### Verify on Master Web UI
- [ ] Active Slaves increments to 1
- [ ] Slave appears in device table
- [ ] Hostname correct
- [ ] IP address correct and clickable
- [ ] Status shows "Active"

### Verify on Slave Web UI
- [ ] Status shows "Connected to master"
- [ ] Assigned slave ID displayed

### Serial Monitor Check
- [ ] Master logs: "Slave registered: [hostname] at slot X"
- [ ] Slave logs: "Registered with master, ID: X"

---

## Test 5: Pool Connection (Master)

### Steps
1. [ ] Configure pool on Master:
   - Pool URL: `stratum+tcp://pool.example.com:3333`
   - Worker name: `your_worker.master`
2. [ ] Save settings
3. [ ] Wait for pool connection

### Verify
- [ ] Master shows "Connected" to pool
- [ ] Master receiving work from pool
- [ ] Serial log shows mining.notify messages

---

## Test 6: Work Distribution

### Expected Behavior
1. Master receives work from pool
2. Master calculates nonce ranges:
   - Master (slot 0): 0x00000000 - 0x7FFFFFFF
   - Slave (slot 1): 0x80000000 - 0xFFFFFFFF
3. Master sends `$CLWRK,...` to slave
4. Slave begins mining assigned range

### Verify on Master
- [ ] Serial log: "Distributing work to X slaves"
- [ ] Serial log: "Sent work to slave X"

### Verify on Slave
- [ ] Serial log: "Received work from master"
- [ ] Serial log: "Job ID: [xxx], Nonce range: [start]-[end]"
- [ ] Hashrate appears on Slave web UI
- [ ] Hashrate appears in Master slave table

---

## Test 7: Heartbeat Monitoring

### Expected Behavior
- Slave sends `$CLHBT,...` every 3 seconds
- Master updates slave stats on each heartbeat
- Master marks slave "Stale" after 10 seconds without heartbeat

### Verify
- [ ] Master slave table updates every few seconds
- [ ] Temperature updates
- [ ] Fan RPM updates
- [ ] Hashrate updates
- [ ] Last Seen timestamp updates

### Test Timeout
1. [ ] Disconnect BAP cable from slave
2. [ ] Wait 10+ seconds
3. [ ] Verify slave shows "Stale" status on master
4. [ ] Reconnect cable
5. [ ] Verify slave recovers to "Active"

---

## Test 8: Share Submission

### Expected Behavior
1. Slave finds valid share
2. Slave sends `$CLSHR,...` to Master
3. Master submits share to pool
4. Pool accepts/rejects share

### Verify
- [ ] Slave "Shares" counter increments
- [ ] Master "Total Shares" counter increments
- [ ] Pool dashboard shows shares from master worker
- [ ] No duplicate shares submitted

### Check Share Attribution
- [ ] All shares submitted under master's worker name
- [ ] Per-slave share counts tracked on master

---

## Test 9: Slave Disconnect/Reconnect

### Steps
1. [ ] Note current slave stats
2. [ ] Power off slave device
3. [ ] Wait 15 seconds
4. [ ] Verify master shows slave as disconnected
5. [ ] Power on slave device
6. [ ] Wait for re-registration

### Verify
- [ ] Slave re-registers automatically
- [ ] Slave receives new work
- [ ] Mining resumes normally
- [ ] No duplicate slave entries

---

## Test 10: Mode Switching

### Test Master → Disabled
1. [ ] On Master web UI, go to Cluster page
2. [ ] Change mode to "Disabled"
3. [ ] Confirm and restart
4. [ ] Verify device operates as standalone miner

### Test Slave → Disabled
1. [ ] On Slave web UI, go to Cluster page
2. [ ] Change mode to "Disabled"
3. [ ] Confirm and restart
4. [ ] Verify device connects to pool independently

---

## Test 11: API Endpoints

### Master API
```bash
# Get cluster status
curl http://<master-ip>/api/cluster/status

# Expected: JSON with mode, activeSlaves, slaves array
```
- [ ] Returns valid JSON
- [ ] Slave data included
- [ ] All fields populated

### Slave API
```bash
# Get system info
curl http://<slave-ip>/api/system/info

# Get mining stats
curl http://<slave-ip>/api/swarm/info
```
- [ ] Returns valid JSON
- [ ] Stats match web UI

---

## Test 12: Extended 24-Hour Test

### Setup
1. [ ] Configure production pool
2. [ ] Connect master + 1 slave
3. [ ] Leave running for 24 hours

### Monitor
- [ ] Check every few hours for:
  - [ ] Both devices still mining
  - [ ] Shares being accepted
  - [ ] No memory leaks (check heap usage)
  - [ ] No UART errors accumulating
  - [ ] Temperatures stable

### Record Results
- Start time: ____________
- End time: ____________
- Total shares (Master): ____________
- Total shares (Slave): ____________
- Accepted: ____________
- Rejected: ____________
- Errors observed: ____________

---

## Known Issues / Bugs Found

| Issue | Severity | Description | Status |
|-------|----------|-------------|--------|
| | | | |
| | | | |
| | | | |

---

## Test Results Summary

| Test | Pass/Fail | Notes |
|------|-----------|-------|
| 1. Flash Master | | |
| 2. Flash Slave | | |
| 3. BAP Cable | | |
| 4. Registration | | |
| 5. Pool Connection | | |
| 6. Work Distribution | | |
| 7. Heartbeat | | |
| 8. Share Submission | | |
| 9. Disconnect/Reconnect | | |
| 10. Mode Switching | | |
| 11. API Endpoints | | |
| 12. 24-Hour Test | | |

---

## Next Steps After Testing

If all tests pass:
1. [ ] Create GitHub repository
2. [ ] Tag v1.0.0 release
3. [ ] Upload firmware binaries
4. [ ] Announce on OSMU Discord
5. [ ] Submit to Bitaxe community

If issues found:
1. [ ] Document bugs in this checklist
2. [ ] Fix firmware issues
3. [ ] Rebuild and re-test
4. [ ] Repeat until all tests pass
