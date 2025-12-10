import { HttpClient } from '@angular/common/http';
import { Injectable } from '@angular/core';
import { delay, Observable, of, timeout } from 'rxjs';
import { environment } from '../../environments/environment';

export interface IClusterSlave {
  slot: number;
  slaveId: number;
  hostname: string;
  ipAddr: string;
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
}

export interface IClusterStatus {
  enabled: boolean;
  mode: number;
  modeString: string;
  // Master fields
  activeSlaves?: number;
  totalHashrate?: number;
  totalShares?: number;
  totalSharesAccepted?: number;
  totalSharesRejected?: number;
  slaves?: IClusterSlave[];
  // Slave fields
  connectedToMaster?: boolean;
  localHashrate?: number;
  localTemperature?: number;
  localFanRpm?: number;
  hostname?: string;
}

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
}
