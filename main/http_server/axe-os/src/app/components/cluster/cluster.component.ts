import { Component, OnDestroy, OnInit } from '@angular/core';
import { Observable, interval, startWith, switchMap, catchError, of, BehaviorSubject } from 'rxjs';
import { ClusterService, IClusterStatus } from '../../services/cluster.service';
import { SystemService } from '../../services/system.service';

@Component({
  selector: 'app-cluster',
  templateUrl: './cluster.component.html',
  styleUrls: ['./cluster.component.scss']
})
export class ClusterComponent implements OnInit, OnDestroy {

  public clusterStatus$: Observable<IClusterStatus | null>;
  public loading$ = new BehaviorSubject<boolean>(true);
  public error$ = new BehaviorSubject<string | null>(null);

  // Mode options for dropdown
  public modeOptions = [
    { label: 'Disabled', value: 0 },
    { label: 'Master', value: 1 },
    { label: 'Slave', value: 2 }
  ];

  public selectedMode: number = 0;
  public showModeChangeDialog = false;
  public pendingMode: number = 0;

  private refreshInterval = 3000; // 3 seconds

  constructor(
    public clusterService: ClusterService,
    private systemService: SystemService
  ) {
    this.clusterStatus$ = interval(this.refreshInterval).pipe(
      startWith(0),
      switchMap(() => this.clusterService.getStatus().pipe(
        catchError(err => {
          this.error$.next('Failed to fetch cluster status');
          this.loading$.next(false);
          return of(null);
        })
      ))
    );
  }

  ngOnInit(): void {
    this.clusterStatus$.subscribe(status => {
      this.loading$.next(false);
      if (status) {
        this.error$.next(null);
        this.selectedMode = status.mode;
      }
    });
  }

  ngOnDestroy(): void {
    // Cleanup handled by async pipe
  }

  onModeChange(event: any): void {
    this.pendingMode = event.value;
    this.showModeChangeDialog = true;
  }

  confirmModeChange(): void {
    this.showModeChangeDialog = false;
    this.clusterService.setMode('', this.pendingMode).subscribe({
      next: () => {
        // Restart required for mode change
        this.systemService.restart().subscribe();
      },
      error: (err) => {
        this.error$.next('Failed to change cluster mode');
        this.selectedMode = this.selectedMode; // Revert
      }
    });
  }

  cancelModeChange(): void {
    this.showModeChangeDialog = false;
    // Revert dropdown to current mode
  }

  formatHashrate(hashrate: number): string {
    // hashrate is in GH/s * 100, so divide by 100
    const gh = hashrate / 100;
    if (gh >= 1000) {
      return (gh / 1000).toFixed(2) + ' TH/s';
    }
    return gh.toFixed(2) + ' GH/s';
  }

  formatLastSeen(timestamp: number): string {
    const now = Date.now();
    const diff = now - timestamp;
    if (diff < 1000) return 'Just now';
    if (diff < 60000) return Math.floor(diff / 1000) + 's ago';
    if (diff < 3600000) return Math.floor(diff / 60000) + 'm ago';
    return Math.floor(diff / 3600000) + 'h ago';
  }

  getSlaveStateIcon(state: number): string {
    switch (state) {
      case 0: return 'pi-times-circle';
      case 1: return 'pi-spin pi-spinner';
      case 2: return 'pi-check-circle';
      case 3: return 'pi-exclamation-triangle';
      default: return 'pi-question-circle';
    }
  }

  getTotalPower(slaves: any[]): number {
    if (!slaves) return 0;
    return slaves.reduce((sum, slave) => sum + (slave.power || 0), 0);
  }
}
