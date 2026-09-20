import { Injectable } from '@angular/core';
import { Observable, Subject, timer } from 'rxjs';
import { retryWhen, delayWhen, tap } from 'rxjs/operators';
import { webSocket, WebSocketSubject } from 'rxjs/webSocket';
import { ISystemInfo } from 'src/models/ISystemInfo';

/**
 * Streams fast-changing telemetry pushed by the firmware over /api/ws/live.
 *
 * Each message is a partial of the RAW /api/system/info payload (same field
 * names and units — e.g. voltage/current in mV/mA). Consumers that also read
 * /api/system/info should apply the same transforms they already apply there
 * (the home dashboard divides voltage/current/coreVoltageActual by 1000).
 *
 * The feed is push-only at ~1 Hz while at least one client is connected; it
 * does not replace /api/system/info, it just fills the gaps between polls.
 */
@Injectable({
  providedIn: 'root'
})
export class LiveDataService {

  private socket$?: WebSocketSubject<Partial<ISystemInfo>>;
  private data$ = new Subject<Partial<ISystemInfo>>();

  public readonly liveData$: Observable<Partial<ISystemInfo>> = this.data$.asObservable();

  public connect(): void {
    if (this.socket$ && !this.socket$.closed) {
      return;
    }

    this.socket$ = webSocket<Partial<ISystemInfo>>({
      url: `ws://${window.location.host}/api/ws/live`,
    });

    this.socket$.pipe(
      // Reconnect with a small backoff if the socket drops.
      retryWhen(errors => errors.pipe(
        tap(() => this.socket$ = undefined),
        delayWhen(() => timer(3000)),
      )),
    ).subscribe({
      next: (msg) => this.data$.next(msg),
      error: () => { /* handled by retryWhen */ },
    });
  }

  public disconnect(): void {
    this.socket$?.complete();
    this.socket$ = undefined;
  }
}
