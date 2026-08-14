import { HttpClient } from '@angular/common/http';
import { Injectable } from '@angular/core';
import { delay, Observable, of, timeout } from 'rxjs';
import { environment } from '../../environments/environment';
import { IWatchdogStatus, IAutoTimingStatus } from './cluster.service';

export { IWatchdogStatus, IAutoTimingStatus };

@Injectable({
  providedIn: 'root'
})
export class TunerService {

  constructor(private httpClient: HttpClient) {}

  public getWatchdogStatus(): Observable<IWatchdogStatus> {
    if (environment.production) {
      return this.httpClient.get<IWatchdogStatus>('/api/cluster/watchdog/status').pipe(timeout(5000));
    }
    return of({
      enabled: true,
      running: true,
      throttledCount: 0,
      tempThreshold: 68,
      vinThreshold: 4.75,
      tempRecoveryThreshold: 62,
      vinRecoveryThreshold: 4.9,
      recoveryStabilityMs: 30000,
      master: {
        throttled: false,
        recovering: false,
        throttleReason: 0,
        temp: 54.2,
        vin: 5.08,
        frequency: 500,
        voltage: 1200,
        originalFrequency: 500,
        originalVoltage: 1200,
        throttleCount: 1
      },
      slaves: []
    }).pipe(delay(400));
  }

  public setWatchdogEnabled(enabled: boolean): Observable<any> {
    if (environment.production) {
      return this.httpClient.post('/api/cluster/watchdog', { enabled }).pipe(timeout(5000));
    }
    return of({ success: true }).pipe(delay(400));
  }

  public getAutoTimingStatus(): Observable<IAutoTimingStatus> {
    if (environment.production) {
      return this.httpClient.get<IAutoTimingStatus>('/api/system/autotiming').pipe(timeout(5000));
    }
    return of({
      enabled: true,
      state: 'monitoring',
      currentInterval: 650,
      optimalInterval: 650,
      minInterval: 500,
      maxInterval: 800,
      rejectionRate: 0.8,
      windowAccepted: 142,
      windowRejected: 1,
      calibrationStep: 0,
      bestInterval: 650,
      bestRejectionRate: 0.7
    }).pipe(delay(400));
  }

  public setAutoTimingEnabled(enabled: boolean): Observable<any> {
    if (environment.production) {
      return this.httpClient.patch('/api/system/autotiming', { enabled }).pipe(timeout(5000));
    }
    return of({ success: true }).pipe(delay(400));
  }

  public setAutoTimingInterval(interval: number): Observable<any> {
    if (environment.production) {
      return this.httpClient.patch('/api/system/autotiming', { interval }).pipe(timeout(5000));
    }
    return of({ success: true }).pipe(delay(400));
  }

  public recalibrateAutoTiming(): Observable<any> {
    if (environment.production) {
      return this.httpClient.patch('/api/system/autotiming', { recalibrate: true }).pipe(timeout(5000));
    }
    return of({ success: true }).pipe(delay(400));
  }
}
