import { Component, OnDestroy, OnInit } from '@angular/core';
import { Observable, interval, startWith, switchMap, catchError, of, BehaviorSubject, Subscription } from 'rxjs';
import { ClusterService, IClusterStatus, IClusterSlave, ISlaveConfig, IAutotuneStatus, CLUSTER_SETTINGS } from '../../services/cluster.service';
import { SystemService } from '../../services/system.service';
import { MessageService } from 'primeng/api';
import { trigger, transition, style, animate } from '@angular/animations';

@Component({
  selector: 'app-cluster',
  templateUrl: './cluster.component.html',
  styleUrls: ['./cluster.component.scss'],
  animations: [
    trigger('fadeInOut', [
      transition(':enter', [
        style({ opacity: 0, height: 0 }),
        animate('200ms ease-out', style({ opacity: 1, height: '*' }))
      ]),
      transition(':leave', [
        animate('200ms ease-in', style({ opacity: 0, height: 0 }))
      ])
    ])
  ]
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

  // Slave configuration panel state
  public expandedSlaveId: number | null = null;
  public slaveConfigs: Map<number, ISlaveConfig> = new Map();
  public loadingSlaveConfig: number | null = null;
  public savingSlaveConfig: number | null = null;

  // Editable slave settings
  public editFrequency: number = 0;
  public editVoltage: number = 0;
  public editFanSpeed: number = 0;
  public editFanMode: number = 0;
  public editTargetTemp: number = 0;

  // Bulk actions
  public showBulkActionDialog = false;
  public bulkActionType: 'frequency' | 'voltage' | 'fan' | 'restart' | null = null;
  public bulkFrequency: number = 500;
  public bulkVoltage: number = 1200;
  public bulkFanSpeed: number = 50;

  // Fan mode options
  public fanModeOptions = [
    { label: 'Auto', value: 0 },
    { label: 'Manual', value: 1 }
  ];

  // Autotune state
  public autotuneStatus: IAutotuneStatus | null = null;
  public autotuneEnabled = false;
  public autotuneLoading = false;
  public showAutotuneDialog = false;
  public selectedAutotuneMode = 'efficiency';
  public autotuneSubscription: Subscription | null = null;

  // Autotune mode options
  public autotuneModeOptions = [
    { label: 'Efficiency (J/TH)', value: 'efficiency' },
    { label: 'Max Hashrate', value: 'hashrate' },
    { label: 'Balanced', value: 'balanced' }
  ];

  // Oscilloscope wave points for animation
  public oscilloscopePoints: number[] = [];

  private refreshInterval = 3000; // 3 seconds

  constructor(
    public clusterService: ClusterService,
    private systemService: SystemService,
    private messageService: MessageService
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

        // Start autotune polling if in master mode
        if (status.mode === 1 && !this.autotuneSubscription) {
          this.startAutotunePolling();
        } else if (status.mode !== 1 && this.autotuneSubscription) {
          this.stopAutotunePolling();
        }
      }
    });

    // Generate initial oscilloscope points
    this.generateOscilloscopePoints();
  }

  ngOnDestroy(): void {
    this.stopAutotunePolling();
  }

  private startAutotunePolling(): void {
    this.autotuneSubscription = interval(2000).pipe(
      startWith(0),
      switchMap(() => this.clusterService.getAutotuneStatus().pipe(
        catchError(() => of(null))
      ))
    ).subscribe(status => {
      if (status) {
        this.autotuneStatus = status;
        this.autotuneEnabled = status.enabled;
        // Regenerate oscilloscope points when status changes
        if (status.running) {
          this.generateOscilloscopePoints();
        }
      }
    });
  }

  private stopAutotunePolling(): void {
    if (this.autotuneSubscription) {
      this.autotuneSubscription.unsubscribe();
      this.autotuneSubscription = null;
    }
  }

  private generateOscilloscopePoints(): void {
    // Generate a sine wave with some noise for the oscilloscope effect
    this.oscilloscopePoints = [];
    for (let i = 0; i < 50; i++) {
      const x = i / 50 * Math.PI * 4;
      const baseWave = Math.sin(x) * 30;
      const noise = (Math.random() - 0.5) * 10;
      this.oscilloscopePoints.push(50 + baseWave + noise);
    }
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

  // ========================================================================
  // Slave Configuration Panel Methods
  // ========================================================================

  toggleSlaveConfig(slave: IClusterSlave): void {
    if (this.expandedSlaveId === slave.slaveId) {
      this.expandedSlaveId = null;
    } else {
      this.expandedSlaveId = slave.slaveId;
      this.loadSlaveConfig(slave.slaveId);
    }
  }

  loadSlaveConfig(slaveId: number): void {
    this.loadingSlaveConfig = slaveId;
    this.clusterService.getSlaveConfig('', slaveId).subscribe({
      next: (config) => {
        this.slaveConfigs.set(slaveId, config);
        this.editFrequency = config.frequency;
        this.editVoltage = config.coreVoltage;
        this.editFanSpeed = config.fanSpeed;
        this.editFanMode = config.fanMode;
        this.editTargetTemp = config.targetTemp;
        this.loadingSlaveConfig = null;
      },
      error: (err) => {
        this.messageService.add({
          severity: 'error',
          summary: 'Error',
          detail: `Failed to load configuration for slave ${slaveId}`
        });
        this.loadingSlaveConfig = null;
      }
    });
  }

  getSlaveConfig(slaveId: number): ISlaveConfig | undefined {
    return this.slaveConfigs.get(slaveId);
  }

  saveSlaveFrequency(slaveId: number): void {
    this.savingSlaveConfig = slaveId;
    this.clusterService.setSlaveFrequency('', slaveId, this.editFrequency).subscribe({
      next: () => {
        this.messageService.add({
          severity: 'success',
          summary: 'Success',
          detail: `Frequency updated to ${this.editFrequency} MHz`
        });
        this.savingSlaveConfig = null;
        // Update local cache
        const config = this.slaveConfigs.get(slaveId);
        if (config) config.frequency = this.editFrequency;
      },
      error: () => {
        this.messageService.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to update frequency'
        });
        this.savingSlaveConfig = null;
      }
    });
  }

  saveSlaveVoltage(slaveId: number): void {
    this.savingSlaveConfig = slaveId;
    this.clusterService.setSlaveVoltage('', slaveId, this.editVoltage).subscribe({
      next: () => {
        this.messageService.add({
          severity: 'success',
          summary: 'Success',
          detail: `Voltage updated to ${this.editVoltage} mV`
        });
        this.savingSlaveConfig = null;
        const config = this.slaveConfigs.get(slaveId);
        if (config) config.coreVoltage = this.editVoltage;
      },
      error: () => {
        this.messageService.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to update voltage'
        });
        this.savingSlaveConfig = null;
      }
    });
  }

  saveSlaveFanSpeed(slaveId: number): void {
    this.savingSlaveConfig = slaveId;
    this.clusterService.setSlaveFanSpeed('', slaveId, this.editFanSpeed).subscribe({
      next: () => {
        this.messageService.add({
          severity: 'success',
          summary: 'Success',
          detail: `Fan speed updated to ${this.editFanSpeed}%`
        });
        this.savingSlaveConfig = null;
        const config = this.slaveConfigs.get(slaveId);
        if (config) config.fanSpeed = this.editFanSpeed;
      },
      error: () => {
        this.messageService.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to update fan speed'
        });
        this.savingSlaveConfig = null;
      }
    });
  }

  saveSlaveFanMode(slaveId: number): void {
    this.savingSlaveConfig = slaveId;
    this.clusterService.setSlaveSetting('', slaveId, {
      settingId: CLUSTER_SETTINGS.FAN_MODE,
      value: this.editFanMode
    }).subscribe({
      next: () => {
        this.messageService.add({
          severity: 'success',
          summary: 'Success',
          detail: `Fan mode updated to ${this.editFanMode === 0 ? 'Auto' : 'Manual'}`
        });
        this.savingSlaveConfig = null;
        const config = this.slaveConfigs.get(slaveId);
        if (config) config.fanMode = this.editFanMode;
      },
      error: () => {
        this.messageService.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to update fan mode'
        });
        this.savingSlaveConfig = null;
      }
    });
  }

  saveSlaveTargetTemp(slaveId: number): void {
    this.savingSlaveConfig = slaveId;
    this.clusterService.setSlaveSetting('', slaveId, {
      settingId: CLUSTER_SETTINGS.TARGET_TEMP,
      value: this.editTargetTemp
    }).subscribe({
      next: () => {
        this.messageService.add({
          severity: 'success',
          summary: 'Success',
          detail: `Target temperature updated to ${this.editTargetTemp}°C`
        });
        this.savingSlaveConfig = null;
        const config = this.slaveConfigs.get(slaveId);
        if (config) config.targetTemp = this.editTargetTemp;
      },
      error: () => {
        this.messageService.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to update target temperature'
        });
        this.savingSlaveConfig = null;
      }
    });
  }

  restartSlave(slaveId: number): void {
    this.clusterService.restartSlave('', slaveId).subscribe({
      next: () => {
        this.messageService.add({
          severity: 'info',
          summary: 'Restarting',
          detail: `Slave ${slaveId} is restarting...`
        });
      },
      error: () => {
        this.messageService.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to restart slave'
        });
      }
    });
  }

  identifySlave(slaveId: number): void {
    this.clusterService.identifySlave('', slaveId).subscribe({
      next: () => {
        this.messageService.add({
          severity: 'info',
          summary: 'Identify',
          detail: `Slave ${slaveId} LED is flashing`
        });
      },
      error: () => {
        this.messageService.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to identify slave'
        });
      }
    });
  }

  // ========================================================================
  // Bulk Action Methods
  // ========================================================================

  openBulkAction(actionType: 'frequency' | 'voltage' | 'fan' | 'restart'): void {
    this.bulkActionType = actionType;
    this.showBulkActionDialog = true;
  }

  cancelBulkAction(): void {
    this.showBulkActionDialog = false;
    this.bulkActionType = null;
  }

  executeBulkAction(): void {
    this.showBulkActionDialog = false;

    switch (this.bulkActionType) {
      case 'frequency':
        this.clusterService.setAllSlavesSetting('', {
          settingId: CLUSTER_SETTINGS.FREQUENCY,
          value: this.bulkFrequency
        }).subscribe({
          next: (result) => {
            this.messageService.add({
              severity: 'success',
              summary: 'Success',
              detail: `Frequency set to ${this.bulkFrequency} MHz on all slaves`
            });
          },
          error: () => {
            this.messageService.add({
              severity: 'error',
              summary: 'Error',
              detail: 'Failed to update frequency on all slaves'
            });
          }
        });
        break;

      case 'voltage':
        this.clusterService.setAllSlavesSetting('', {
          settingId: CLUSTER_SETTINGS.CORE_VOLTAGE,
          value: this.bulkVoltage
        }).subscribe({
          next: () => {
            this.messageService.add({
              severity: 'success',
              summary: 'Success',
              detail: `Voltage set to ${this.bulkVoltage} mV on all slaves`
            });
          },
          error: () => {
            this.messageService.add({
              severity: 'error',
              summary: 'Error',
              detail: 'Failed to update voltage on all slaves'
            });
          }
        });
        break;

      case 'fan':
        this.clusterService.setAllSlavesSetting('', {
          settingId: CLUSTER_SETTINGS.FAN_SPEED,
          value: this.bulkFanSpeed
        }).subscribe({
          next: () => {
            this.messageService.add({
              severity: 'success',
              summary: 'Success',
              detail: `Fan speed set to ${this.bulkFanSpeed}% on all slaves`
            });
          },
          error: () => {
            this.messageService.add({
              severity: 'error',
              summary: 'Error',
              detail: 'Failed to update fan speed on all slaves'
            });
          }
        });
        break;

      case 'restart':
        this.clusterService.restartAllSlaves('').subscribe({
          next: () => {
            this.messageService.add({
              severity: 'info',
              summary: 'Restarting',
              detail: 'All slaves are restarting...'
            });
          },
          error: () => {
            this.messageService.add({
              severity: 'error',
              summary: 'Error',
              detail: 'Failed to restart all slaves'
            });
          }
        });
        break;
    }

    this.bulkActionType = null;
  }

  getBulkActionTitle(): string {
    switch (this.bulkActionType) {
      case 'frequency': return 'Set Frequency on All Slaves';
      case 'voltage': return 'Set Voltage on All Slaves';
      case 'fan': return 'Set Fan Speed on All Slaves';
      case 'restart': return 'Restart All Slaves';
      default: return 'Bulk Action';
    }
  }

  // ========================================================================
  // Transport Info Helpers
  // ========================================================================

  getTransportIcon(transportType: string | undefined): string {
    if (!transportType) return 'pi-question-circle';
    switch (transportType.toLowerCase()) {
      case 'espnow': return 'pi-wifi';
      case 'bap': return 'pi-link';
      default: return 'pi-question-circle';
    }
  }

  getTransportLabel(transportType: string | undefined): string {
    if (!transportType) return 'Unknown';
    switch (transportType.toLowerCase()) {
      case 'espnow': return 'ESP-NOW (Wireless)';
      case 'bap': return 'BAP (UART Cable)';
      default: return transportType;
    }
  }

  // ========================================================================
  // Autotune Methods
  // ========================================================================

  toggleAutotune(): void {
    this.autotuneLoading = true;
    const newState = !this.autotuneEnabled;

    this.clusterService.enableMasterAutotune('', newState).subscribe({
      next: () => {
        this.autotuneEnabled = newState;
        this.autotuneLoading = false;
        this.messageService.add({
          severity: 'success',
          summary: 'Success',
          detail: `Autotune ${newState ? 'enabled' : 'disabled'}`
        });
      },
      error: () => {
        this.autotuneLoading = false;
        this.messageService.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to toggle autotune'
        });
      }
    });
  }

  openAutotuneDialog(): void {
    this.showAutotuneDialog = true;
  }

  startAutotune(): void {
    this.showAutotuneDialog = false;
    this.autotuneLoading = true;

    this.clusterService.startAutotune('', this.selectedAutotuneMode).subscribe({
      next: () => {
        this.autotuneLoading = false;
        this.messageService.add({
          severity: 'info',
          summary: 'Autotune Started',
          detail: `Optimizing for ${this.selectedAutotuneMode}`
        });
      },
      error: () => {
        this.autotuneLoading = false;
        this.messageService.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to start autotune'
        });
      }
    });
  }

  stopAutotune(applyBest: boolean = true): void {
    this.autotuneLoading = true;

    this.clusterService.stopAutotune('', applyBest).subscribe({
      next: () => {
        this.autotuneLoading = false;
        this.messageService.add({
          severity: 'success',
          summary: 'Autotune Stopped',
          detail: applyBest ? 'Best settings applied' : 'Settings reverted'
        });
      },
      error: () => {
        this.autotuneLoading = false;
        this.messageService.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to stop autotune'
        });
      }
    });
  }

  getAutotuneStateLabel(stateCode: number): string {
    switch (stateCode) {
      case 0: return 'Idle';
      case 1: return 'Starting';
      case 2: return 'Testing';
      case 3: return 'Adjusting';
      case 4: return 'Stabilizing';
      case 5: return 'Locked';
      case 6: return 'Error';
      default: return 'Unknown';
    }
  }

  getAutotuneStateClass(stateCode: number): string {
    switch (stateCode) {
      case 0: return 'text-500';         // Idle
      case 1: return 'text-blue-500';    // Starting
      case 2: return 'text-cyan-500';    // Testing
      case 3: return 'text-orange-500';  // Adjusting
      case 4: return 'text-yellow-500';  // Stabilizing
      case 5: return 'text-green-500';   // Locked
      case 6: return 'text-red-500';     // Error
      default: return 'text-500';
    }
  }

  getOscilloscopePath(): string {
    if (this.oscilloscopePoints.length === 0) return '';
    const points = this.oscilloscopePoints.map((y, i) => {
      const x = (i / (this.oscilloscopePoints.length - 1)) * 200;
      return `${x},${y}`;
    });
    return `M ${points.join(' L ')}`;
  }

  formatDuration(ms: number): string {
    if (ms < 1000) return `${ms}ms`;
    if (ms < 60000) return `${(ms / 1000).toFixed(1)}s`;
    return `${(ms / 60000).toFixed(1)}m`;
  }
}
