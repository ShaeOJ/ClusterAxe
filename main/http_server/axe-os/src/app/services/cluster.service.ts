import { HttpClient } from '@angular/common/http';
import { Injectable } from '@angular/core';
import { delay, Observable, of, timeout } from 'rxjs';
import { environment } from '../../environments/environment';

export interface IClusterSlave {
  slot: number;
  slaveId: number;
  hostname: string;
  ipAddr: string;
  macAddr?: string;
  state: number;
  hashrate: number;
  temperature: number;
  fanRpm: number;
  sharesSubmitted: number;
  sharesAccepted: number;
  lastSeen: number;
  // Extended stats
  frequency: number;
  coreVoltage: number;
  power: number;
  voltageIn: number;
  // ESP-NOW specific
  rssi?: number;
}

export interface ISlaveConfig {
  hostname: string;
  deviceModel: string;
  fwVersion: string;
  uptime: number;
  freeHeap: number;
  frequency: number;
  coreVoltage: number;
  fanSpeed: number;
  fanMode: number;
  targetTemp: number;
  hashrate: number;
  power: number;
  efficiency: number;
  chipTemp: number;
}

export interface ISlaveSettingRequest {
  settingId: number;
  value: number | string | boolean;
}

export interface ISlaveCommandRequest {
  command: string;
  params?: any;
}

export interface ITransportInfo {
  type: string;
  channel?: number;
  encrypted?: boolean;
  discoveryActive?: boolean;
  selfMac?: string;
  peerCount?: number;
}

export interface IAutotuneStatus {
  state: string;
  stateCode: number;
  mode: string;
  enabled: boolean;
  running: boolean;
  currentFrequency: number;
  currentVoltage: number;
  bestFrequency: number;
  bestVoltage: number;
  bestEfficiency: number;
  bestHashrate: number;
  progress: number;
  testsCompleted: number;
  testsTotal: number;
  testDuration: number;
  totalDuration: number;
  error?: string;
  // Safety watchdog
  watchdogEnabled?: boolean;
  watchdogRunning?: boolean;
  // Current device being tuned (-1 = master, 0-7 = slave)
  currentDevice?: number;
}

export interface IAutotuneProfile {
  slot?: number;
  name: string;
  frequency: number;
  voltage: number;
  fanSpeed?: number;
  targetTemp?: number;
  savedAt?: number;
}

export interface IProfilesResponse {
  profiles: IAutotuneProfile[];
}

export interface IClusterStatus {
  enabled: boolean;
  mode: number;
  modeString: string;
  // Transport info
  transport?: ITransportInfo;
  // Master fields
  activeSlaves?: number;
  totalHashrate?: number;
  totalPower?: number;
  totalEfficiency?: number;
  totalShares?: number;
  totalSharesAccepted?: number;
  totalSharesRejected?: number;
  currentTime?: number;  // Device time (ms since boot) for calculating last seen
  slaves?: IClusterSlave[];
  // Slave fields
  connectedToMaster?: boolean;
  localHashrate?: number;
  localTemperature?: number;
  localFanRpm?: number;
  hostname?: string;
  sharesFound?: number;
  sharesSubmitted?: number;
}

// Setting IDs for remote configuration
export const CLUSTER_SETTINGS = {
  // Mining settings (0x20 - 0x3F)
  FREQUENCY: 0x20,
  CORE_VOLTAGE: 0x21,
  FAN_SPEED: 0x22,
  FAN_MODE: 0x23,
  TARGET_TEMP: 0x24,
};

@Injectable({
  providedIn: 'root'
})
export class ClusterService {

  constructor(
    private httpClient: HttpClient
  ) { }

  public getStatus(uri: string = ''): Observable<IClusterStatus> {
    if (environment.production) {
      return this.httpClient.get<IClusterStatus>(`${uri}/api/cluster/status`).pipe(timeout(5000));
    }

    // Mock data for development - simulate master mode
    return of({
      enabled: true,
      mode: 1,
      modeString: 'master',
      activeSlaves: 2,
      totalHashrate: 120000,
      totalPower: 36.8,
      totalEfficiency: 24.5,
      totalShares: 256,
      totalSharesAccepted: 250,
      totalSharesRejected: 6,
      slaves: [
        {
          slot: 0,
          slaveId: 1,
          hostname: 'bitaxe-slave-1',
          ipAddr: '192.168.1.101',
          state: 2,
          hashrate: 40000,
          temperature: 52.5,
          fanRpm: 4200,
          sharesSubmitted: 128,
          sharesAccepted: 125,
          lastSeen: Date.now(),
          frequency: 550,
          coreVoltage: 1200,
          power: 12.5,
          voltageIn: 5.1
        },
        {
          slot: 1,
          slaveId: 2,
          hostname: 'bitaxe-slave-2',
          ipAddr: '192.168.1.102',
          state: 2,
          hashrate: 38000,
          temperature: 54.2,
          fanRpm: 4500,
          sharesSubmitted: 128,
          sharesAccepted: 125,
          lastSeen: Date.now(),
          frequency: 525,
          coreVoltage: 1180,
          power: 11.8,
          voltageIn: 5.0
        }
      ]
    }).pipe(delay(500));
  }

  public setMode(uri: string = '', mode: number): Observable<any> {
    if (environment.production) {
      return this.httpClient.post(`${uri}/api/cluster/mode`, { mode }).pipe(timeout(5000));
    }
    return of({ success: true }).pipe(delay(500));
  }

  public getSlaveStateLabel(state: number): string {
    switch (state) {
      case 0: return 'Disconnected';
      case 1: return 'Registering';
      case 2: return 'Active';
      case 3: return 'Stale';
      default: return 'Unknown';
    }
  }

  public getSlaveStateClass(state: number): string {
    switch (state) {
      case 0: return 'text-red-500';
      case 1: return 'text-yellow-500';
      case 2: return 'text-green-500';
      case 3: return 'text-orange-500';
      default: return 'text-500';
    }
  }

  // ========================================================================
  // Remote Slave Configuration API
  // ========================================================================

  /**
   * Get full configuration from a slave
   */
  public getSlaveConfig(uri: string = '', slaveId: number): Observable<ISlaveConfig> {
    if (environment.production) {
      return this.httpClient.get<ISlaveConfig>(`${uri}/api/cluster/slave/${slaveId}/config`).pipe(timeout(5000));
    }
    // Mock data for development
    return of({
      hostname: `bitaxe-slave-${slaveId}`,
      deviceModel: 'Bitaxe Gamma',
      fwVersion: '2.12.0-cluster',
      uptime: 3600 * 24,
      freeHeap: 120000,
      frequency: 550,
      coreVoltage: 1200,
      fanSpeed: 65,
      fanMode: 0,
      targetTemp: 55,
      hashrate: 425,
      power: 12.5,
      efficiency: 29.4,
      chipTemp: 52.5
    }).pipe(delay(500));
  }

  /**
   * Set a single setting on a slave
   */
  public setSlaveSetting(uri: string = '', slaveId: number, request: ISlaveSettingRequest): Observable<any> {
    if (environment.production) {
      return this.httpClient.post(`${uri}/api/cluster/slave/${slaveId}/setting`, request).pipe(timeout(5000));
    }
    return of({ success: true, slaveId, settingId: request.settingId }).pipe(delay(500));
  }

  /**
   * Set a setting on ALL slaves
   */
  public setAllSlavesSetting(uri: string = '', request: ISlaveSettingRequest): Observable<any> {
    if (environment.production) {
      return this.httpClient.post(`${uri}/api/cluster/slaves/setting`, request).pipe(timeout(10000));
    }
    return of({ success: true, affectedSlaves: 2 }).pipe(delay(1000));
  }

  /**
   * Execute a command on a slave
   */
  public sendSlaveCommand(uri: string = '', slaveId: number, request: ISlaveCommandRequest): Observable<any> {
    if (environment.production) {
      return this.httpClient.post(`${uri}/api/cluster/slave/${slaveId}/command`, request).pipe(timeout(5000));
    }
    return of({ success: true, slaveId, command: request.command }).pipe(delay(500));
  }

  /**
   * Execute a command on ALL slaves
   */
  public sendAllSlavesCommand(uri: string = '', request: ISlaveCommandRequest): Observable<any> {
    if (environment.production) {
      return this.httpClient.post(`${uri}/api/cluster/slaves/command`, request).pipe(timeout(10000));
    }
    return of({ success: true, affectedSlaves: 2, command: request.command }).pipe(delay(1000));
  }

  // Convenience methods
  public setSlaveFrequency(uri: string = '', slaveId: number, frequency: number): Observable<any> {
    return this.setSlaveSetting(uri, slaveId, { settingId: CLUSTER_SETTINGS.FREQUENCY, value: frequency });
  }

  public setSlaveVoltage(uri: string = '', slaveId: number, voltage: number): Observable<any> {
    return this.setSlaveSetting(uri, slaveId, { settingId: CLUSTER_SETTINGS.CORE_VOLTAGE, value: voltage });
  }

  public setSlaveFanSpeed(uri: string = '', slaveId: number, speed: number): Observable<any> {
    return this.setSlaveSetting(uri, slaveId, { settingId: CLUSTER_SETTINGS.FAN_SPEED, value: speed });
  }

  public restartSlave(uri: string = '', slaveId: number): Observable<any> {
    return this.sendSlaveCommand(uri, slaveId, { command: 'restart' });
  }

  public restartAllSlaves(uri: string = ''): Observable<any> {
    return this.sendAllSlavesCommand(uri, { command: 'restart' });
  }

  public identifySlave(uri: string = '', slaveId: number): Observable<any> {
    return this.sendSlaveCommand(uri, slaveId, { command: 'identify' });
  }

  // ========================================================================
  // Autotune API
  // ========================================================================

  public getAutotuneStatus(uri: string = ''): Observable<IAutotuneStatus> {
    if (environment.production) {
      return this.httpClient.get<IAutotuneStatus>(`${uri}/api/cluster/autotune/status`).pipe(timeout(5000));
    }
    // Mock data for development
    return of({
      state: 'testing',
      stateCode: 2,
      mode: 'efficiency',
      enabled: true,
      running: true,
      currentFrequency: 525,
      currentVoltage: 1200,
      bestFrequency: 500,
      bestVoltage: 1150,
      bestEfficiency: 17.5,
      bestHashrate: 1.2,
      progress: 45,
      testsCompleted: 9,
      testsTotal: 20,
      testDuration: 30000,
      totalDuration: 120000
    }).pipe(delay(500));
  }

  public enableMasterAutotune(uri: string = '', enable: boolean): Observable<any> {
    if (environment.production) {
      const action = enable ? 'enableMaster' : 'disableMaster';
      return this.httpClient.post(`${uri}/api/cluster/autotune`, { action }).pipe(timeout(5000));
    }
    return of({ success: true }).pipe(delay(500));
  }

  public startAutotune(uri: string = '', mode: string = 'efficiency'): Observable<any> {
    if (environment.production) {
      return this.httpClient.post(`${uri}/api/cluster/autotune`, { action: 'start', mode }).pipe(timeout(5000));
    }
    return of({ success: true }).pipe(delay(500));
  }

  public stopAutotune(uri: string = '', applyBest: boolean = true): Observable<any> {
    if (environment.production) {
      return this.httpClient.post(`${uri}/api/cluster/autotune`, { action: 'stop', applyBest }).pipe(timeout(5000));
    }
    return of({ success: true }).pipe(delay(500));
  }

  public setWatchdog(uri: string = '', enabled: boolean): Observable<any> {
    if (environment.production) {
      const action = enabled ? 'enableWatchdog' : 'disableWatchdog';
      return this.httpClient.post(`${uri}/api/cluster/autotune`, { action }).pipe(timeout(5000));
    }
    return of({ success: true }).pipe(delay(500));
  }

  // ========================================================================
  // Slave Autotune API (via HTTP proxy)
  // ========================================================================

  /**
   * Get autotune status from a specific slave (proxied via master)
   */
  public getSlaveAutotuneStatus(uri: string = '', slaveId: number): Observable<IAutotuneStatus> {
    if (environment.production) {
      return this.httpClient.get<IAutotuneStatus>(`${uri}/api/cluster/slave/${slaveId}/autotune/status`).pipe(timeout(10000));
    }
    // Mock data for development
    return of({
      state: 'idle',
      stateCode: 0,
      mode: 'efficiency',
      enabled: false,
      running: false,
      currentFrequency: 500,
      currentVoltage: 1150,
      bestFrequency: 0,
      bestVoltage: 0,
      bestEfficiency: 0,
      bestHashrate: 0,
      progress: 0,
      testsCompleted: 0,
      testsTotal: 0,
      testDuration: 0,
      totalDuration: 0
    }).pipe(delay(500));
  }

  /**
   * Start autotune on a specific slave
   */
  public startSlaveAutotune(uri: string = '', slaveId: number, mode: string = 'efficiency'): Observable<any> {
    if (environment.production) {
      return this.httpClient.post(`${uri}/api/cluster/slave/${slaveId}/autotune`, { action: 'start', mode }).pipe(timeout(10000));
    }
    return of({ success: true }).pipe(delay(500));
  }

  /**
   * Stop autotune on a specific slave
   */
  public stopSlaveAutotune(uri: string = '', slaveId: number, applyBest: boolean = true): Observable<any> {
    if (environment.production) {
      return this.httpClient.post(`${uri}/api/cluster/slave/${slaveId}/autotune`, { action: 'stop', applyBest }).pipe(timeout(10000));
    }
    return of({ success: true }).pipe(delay(500));
  }

  /**
   * Enable/disable autotune on a specific slave
   */
  public setSlaveAutotuneEnabled(uri: string = '', slaveId: number, enable: boolean): Observable<any> {
    if (environment.production) {
      const action = enable ? 'enable' : 'disable';
      return this.httpClient.post(`${uri}/api/cluster/slave/${slaveId}/autotune`, { action }).pipe(timeout(10000));
    }
    return of({ success: true }).pipe(delay(500));
  }

  // ========================================================================
  // Autotune Profiles API
  // ========================================================================

  public getProfiles(uri: string = ''): Observable<IProfilesResponse> {
    if (environment.production) {
      return this.httpClient.get<IProfilesResponse>(`${uri}/api/cluster/profiles`).pipe(timeout(5000));
    }
    // Mock data for development
    return of({
      profiles: [
        {
          slot: 0,
          name: 'Efficiency Mode',
          frequency: 500,
          voltage: 1150,
          fanSpeed: 50,
          targetTemp: 55,
          savedAt: Date.now() / 1000 - 86400
        },
        {
          slot: 1,
          name: 'Max Performance',
          frequency: 600,
          voltage: 1250,
          fanSpeed: 80,
          targetTemp: 60,
          savedAt: Date.now() / 1000 - 3600
        }
      ]
    }).pipe(delay(500));
  }

  public saveProfile(uri: string = '', profile: IAutotuneProfile): Observable<any> {
    if (environment.production) {
      return this.httpClient.post(`${uri}/api/cluster/profile`, profile).pipe(timeout(5000));
    }
    return of({ success: true, slot: 0 }).pipe(delay(500));
  }

  public deleteProfile(uri: string = '', slot: number): Observable<any> {
    if (environment.production) {
      return this.httpClient.delete(`${uri}/api/cluster/profile/${slot}`).pipe(timeout(5000));
    }
    return of({ success: true }).pipe(delay(500));
  }

  public applyProfile(uri: string = '', slot: number, target: string = 'master', slaveId?: number): Observable<any> {
    if (environment.production) {
      const body: any = { target };
      if (slaveId !== undefined) {
        body.slaveId = slaveId;
      }
      return this.httpClient.post(`${uri}/api/cluster/profile/${slot}/apply`, body).pipe(timeout(10000));
    }
    return of({ success: true, appliedCount: 2 }).pipe(delay(500));
  }

  // RSSI helpers for ESP-NOW
  public rssiToPercent(rssi: number): number {
    // Convert RSSI (typically -100 to -30 dBm) to percentage
    if (rssi >= -30) return 100;
    if (rssi <= -100) return 0;
    return Math.round(((rssi + 100) / 70) * 100);
  }

  public getRssiClass(rssi: number): string {
    if (rssi >= -50) return 'text-green-500';   // Excellent
    if (rssi >= -60) return 'text-green-400';   // Good
    if (rssi >= -70) return 'text-yellow-500';  // Fair
    if (rssi >= -80) return 'text-orange-500';  // Weak
    return 'text-red-500';                       // Poor
  }

  public getRssiLabel(rssi: number): string {
    if (rssi >= -50) return 'Excellent';
    if (rssi >= -60) return 'Good';
    if (rssi >= -70) return 'Fair';
    if (rssi >= -80) return 'Weak';
    return 'Poor';
  }
}
