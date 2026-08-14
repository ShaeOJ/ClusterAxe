import { Component, OnDestroy, OnInit } from '@angular/core';
import { interval, Observable, Subscription, startWith, switchMap, catchError, of } from 'rxjs';
import { TunerService, IWatchdogStatus, IAutoTimingStatus } from '../../services/tuner.service';
import { BenchmarkService, BenchmarkConfig, BenchmarkState, BenchmarkMode } from '../../services/benchmark.service';
import { ClusterService } from '../../services/cluster.service';
import { MessageService } from 'primeng/api';

@Component({
  selector: 'app-tuner',
  templateUrl: './tuner.component.html',
  styleUrls: ['./tuner.component.scss']
})
export class TunerComponent implements OnInit, OnDestroy {

  public watchdog: IWatchdogStatus | null = null;
  public autoTiming: IAutoTimingStatus | null = null;

  public isSlaveMode = false;
  public isMasterMode = false;

  public watchdogLoading = true;
  public autoTimingLoading = true;
  public watchdogToggling = false;
  public autoTimingToggling = false;
  public recalibrating = false;

  // Benchmark
  public benchmarkState$: Observable<BenchmarkState>;
  public benchmarkConfig: BenchmarkConfig;
  public benchmarkLogs: string[] = [];
  public showBenchmark = false;
  private benchmarkLogSub: Subscription | null = null;

  private pollInterval = 5000;
  private subs: Subscription[] = [];

  constructor(
    private tunerService: TunerService,
    private messageService: MessageService,
    private clusterService: ClusterService,
    public benchmarkService: BenchmarkService
  ) {
    this.benchmarkState$ = this.benchmarkService.getState();
    this.benchmarkConfig = this.benchmarkService.getDefaultConfig('efficiency');
  }

  ngOnInit(): void {
    this.subs.push(
      this.clusterService.getStatus().pipe(catchError(() => of(null))).subscribe(status => {
        if (status) {
          this.isSlaveMode = status.mode === 2;
          this.isMasterMode = status.mode === 1;
        }
      })
    );

    this.subs.push(
      interval(this.pollInterval).pipe(
        startWith(0),
        switchMap(() => this.tunerService.getWatchdogStatus().pipe(catchError(() => of(null))))
      ).subscribe(status => {
        this.watchdogLoading = false;
        if (status) this.watchdog = status;
      })
    );

    this.subs.push(
      interval(this.pollInterval).pipe(
        startWith(0),
        switchMap(() => this.tunerService.getAutoTimingStatus().pipe(catchError(() => of(null))))
      ).subscribe(status => {
        this.autoTimingLoading = false;
        if (status) this.autoTiming = status;
      })
    );
  }

  ngOnDestroy(): void {
    this.subs.forEach(s => s.unsubscribe());
    this.benchmarkLogSub?.unsubscribe();
  }

  toggleWatchdog(): void {
    if (!this.watchdog || this.watchdogToggling) return;
    this.watchdogToggling = true;
    // ngModel updates watchdog.enabled before onChange fires, so read it directly
    const next = this.watchdog.enabled;
    this.tunerService.setWatchdogEnabled(next).subscribe({
      next: () => {
        this.messageService.add({ severity: 'success', summary: 'Watchdog', detail: next ? 'Enabled' : 'Disabled' });
        this.watchdogToggling = false;
      },
      error: () => {
        // Revert toggle on failure
        if (this.watchdog) this.watchdog.enabled = !next;
        this.messageService.add({ severity: 'error', summary: 'Watchdog', detail: 'Failed to update' });
        this.watchdogToggling = false;
      }
    });
  }

  toggleAutoTiming(): void {
    if (!this.autoTiming || this.autoTimingToggling) return;
    this.autoTimingToggling = true;
    // ngModel updates autoTiming.enabled before onChange fires, so read it directly
    const next = this.autoTiming.enabled;
    this.tunerService.setAutoTimingEnabled(next).subscribe({
      next: () => {
        this.messageService.add({ severity: 'success', summary: 'Auto-Tuner', detail: next ? 'Enabled' : 'Disabled' });
        this.autoTimingToggling = false;
      },
      error: () => {
        // Revert toggle on failure
        if (this.autoTiming) this.autoTiming.enabled = !next;
        this.messageService.add({ severity: 'error', summary: 'Auto-Tuner', detail: 'Failed to update' });
        this.autoTimingToggling = false;
      }
    });
  }

  recalibrate(): void {
    if (this.recalibrating) return;
    this.recalibrating = true;
    this.tunerService.recalibrateAutoTiming().subscribe({
      next: () => {
        this.messageService.add({ severity: 'info', summary: 'Auto-Tuner', detail: 'Calibration restarted' });
        this.recalibrating = false;
      },
      error: () => {
        this.messageService.add({ severity: 'error', summary: 'Auto-Tuner', detail: 'Failed to recalibrate' });
        this.recalibrating = false;
      }
    });
  }

  get watchdogStatusLabel(): string {
    if (!this.watchdog) return 'Unknown';
    const m = this.watchdog.master;
    if (m.throttled) return 'THROTTLED';
    if (m.recovering) return 'RECOVERING';
    return 'OK';
  }

  get watchdogStatusSeverity(): 'success' | 'info' | 'warning' | 'danger' | 'secondary' {
    if (!this.watchdog) return 'secondary';
    const m = this.watchdog.master;
    if (m.throttled) return 'danger';
    if (m.recovering) return 'warning';
    return 'success';
  }

  get autoTimingStateSeverity(): 'success' | 'info' | 'warning' | 'danger' | 'secondary' {
    if (!this.autoTiming) return 'secondary';
    switch (this.autoTiming.state) {
      case 'calibrating': return 'warning';
      case 'monitoring':  return 'info';
      case 'locked':      return 'success';
      default:            return 'secondary';
    }
  }

  get calibrationProgress(): number {
    if (!this.autoTiming || this.autoTiming.state !== 'calibrating') return 0;
    return Math.round((this.autoTiming.calibrationStep / 7) * 100);
  }

  // ── Benchmark ──────────────────────────────────────────────────────────────

  openBenchmark(mode: BenchmarkMode): void {
    this.benchmarkConfig = { ...this.benchmarkService.getDefaultConfig(mode), targetIp: '', targetName: 'This Device' };
    this.benchmarkLogs = [];
    this.showBenchmark = true;
    this.benchmarkLogSub?.unsubscribe();
    this.benchmarkLogSub = this.benchmarkService.getLogs().subscribe(log => {
      this.benchmarkLogs.push(log);
      if (this.benchmarkLogs.length > 100) this.benchmarkLogs.shift();
    });
  }

  closeBenchmark(): void {
    this.showBenchmark = false;
    this.benchmarkLogSub?.unsubscribe();
    this.benchmarkLogSub = null;
  }

  startBenchmark(): void {
    this.benchmarkLogs = [];
    this.benchmarkService.startBenchmark(this.benchmarkConfig);
  }

  stopBenchmark(): void {
    this.benchmarkService.stopBenchmark();
  }

  async applyBenchmarkResult(): Promise<void> {
    try {
      await this.benchmarkService.applyBestResult('');
      this.messageService.add({ severity: 'success', summary: 'Benchmark', detail: 'Best settings applied' });
      this.closeBenchmark();
    } catch (e: any) {
      this.messageService.add({ severity: 'error', summary: 'Benchmark', detail: e.message || 'Failed to apply settings' });
    }
  }
}
