import { Component, OnDestroy, OnInit } from '@angular/core';
import { Observable, interval, startWith, switchMap, catchError, of, BehaviorSubject, Subscription } from 'rxjs';
import { ClusterService, IClusterStatus, IClusterSlave, ISlaveConfig, IWatchdogStatus, CLUSTER_SETTINGS } from '../../services/cluster.service';
import { SystemService } from '../../services/system.service';
import { BenchmarkService, BenchmarkConfig, BenchmarkState } from '../../services/benchmark.service';
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
  public deviceCurrentTime: number = 0;  // Device time (ms since boot) for last seen calculation

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

  // Master device settings
  public masterInfo: any = null;
  public masterFrequency: number = 500;
  public masterVoltage: number = 1200;
  public masterFanSpeed: number = 50;
  public masterFanMode: number = 0;
  public masterTargetTemp: number = 55;
  public masterExpanded: boolean = false;
  public savingMasterConfig: boolean = false;
  private masterEditValuesInitialized: boolean = false;

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

  // Watchdog state
  public watchdogStatus: IWatchdogStatus | null = null;
  public watchdogLoading = false;
  public watchdogToggling = false;
  private watchdogSubscription: Subscription | null = null;

  // Benchmark state
  public showBenchmarkDialog = false;
  public benchmarkState$: Observable<BenchmarkState>;
  public benchmarkLogs: string[] = [];
  public benchmarkConfig: BenchmarkConfig;
  public benchmarkTargetIp: string = '';
  public benchmarkTargetName: string = 'Master';
  private benchmarkLogSubscription: Subscription | null = null;

  private refreshInterval = 3000; // 3 seconds

  constructor(
    public clusterService: ClusterService,
    private systemService: SystemService,
    private messageService: MessageService,
    public benchmarkService: BenchmarkService
  ) {
    this.benchmarkState$ = this.benchmarkService.getState();
    this.benchmarkConfig = this.benchmarkService.getDefaultConfig();
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

        // Store device current time for last seen calculations
        if (status.currentTime) {
          this.deviceCurrentTime = status.currentTime;
        }

        // Refresh master info on every poll to keep stats updated
        if (status.mode === 1) {
          this.loadMasterInfo();
          // Start watchdog polling if in master mode
          this.startWatchdogPolling();
        } else {
          // Stop watchdog polling if not in master mode
          this.stopWatchdogPolling();
        }
      }
    });
  }

  ngOnDestroy(): void {
    this.stopWatchdogPolling();
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

  calcSlaveEfficiency(slave: any): number {
    // hashrate is in GH/s * 100, power is in W
    // Efficiency = Power (W) / Hashrate (TH/s)
    // TH/s = (hashrate / 100) / 1000 = hashrate / 100000
    const hashrateTh = slave.hashrate / 100000;
    if (hashrateTh <= 0 || !slave.power) {
      return 0;
    }
    return slave.power / hashrateTh;
  }

  formatLastSeen(timestamp: number): string {
    // Use device current time (ms since boot) to calculate difference
    const diff = this.deviceCurrentTime - timestamp;
    if (diff < 0 || diff > 86400000) return 'Unknown';  // Sanity check
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

  getPoolSplitPercent(status: IClusterStatus, pool: 'primary' | 'secondary'): number {
    const primaryTotal = (status.primarySharesAccepted || 0) + (status.primarySharesRejected || 0);
    const secondaryTotal = (status.secondarySharesAccepted || 0) + (status.secondarySharesRejected || 0);
    const total = primaryTotal + secondaryTotal;
    if (total === 0) return pool === 'primary' ? 100 : 0;
    const value = pool === 'primary' ? primaryTotal : secondaryTotal;
    return Math.round((value / total) * 100);
  }

  // TrackBy function to prevent ngFor from recreating DOM elements
  trackBySlave(index: number, slave: IClusterSlave): number {
    return slave.slot;
  }

  // ========================================================================
  // Slave Configuration Panel Methods
  // ========================================================================

  toggleSlaveConfig(slave: IClusterSlave): void {
    if (this.expandedSlaveId === slave.slot) {
      this.expandedSlaveId = null;
    } else {
      this.expandedSlaveId = slave.slot;
      this.loadSlaveConfig(slave.slot);
    }
  }

  loadSlaveConfig(slot: number): void {
    this.loadingSlaveConfig = slot;
    this.clusterService.getSlaveConfig('', slot).subscribe({
      next: (config) => {
        this.slaveConfigs.set(slot, config);
        this.editFrequency = config.frequency;
        // Validate voltage - if it's way too high, it's likely wrong data from old firmware
        // Valid range is 1000-1300 mV
        if (config.coreVoltage > 1500) {
          this.editVoltage = 1200; // Default to safe value
          console.warn(`Slave ${slot} reported invalid voltage ${config.coreVoltage}mV - using default 1200mV. Rebuild slave firmware.`);
        } else {
          this.editVoltage = config.coreVoltage;
        }
        this.editFanSpeed = config.fanSpeed;
        this.editFanMode = config.fanMode;
        this.editTargetTemp = config.targetTemp;
        this.loadingSlaveConfig = null;
      },
      error: (err) => {
        this.messageService.add({
          severity: 'error',
          summary: 'Error',
          detail: `Failed to load configuration for slave ${slot}`
        });
        this.loadingSlaveConfig = null;
      }
    });
  }

  getSlaveConfig(slot: number): ISlaveConfig | undefined {
    return this.slaveConfigs.get(slot);
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
  // Master Device Settings Methods
  // ========================================================================

  loadMasterInfo(): void {
    this.systemService.getInfo('').subscribe({
      next: (info) => {
        this.masterInfo = info;
        // Only initialize editable values once (when panel is first expanded)
        // This prevents overwriting user input during polling
        if (!this.masterEditValuesInitialized) {
          this.masterFrequency = info.frequency || 500;
          this.masterVoltage = info.coreVoltage || 1200;
          this.masterFanSpeed = info.fanspeed || 50;
          this.masterFanMode = info.autofanspeed === 1 ? 0 : 1; // 0 = auto, 1 = manual
          this.masterTargetTemp = info.autofanspeed === 1 ? 55 : this.masterTargetTemp;
          this.masterEditValuesInitialized = true;
        }
      },
      error: () => {
        this.messageService.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to load master device info'
        });
      }
    });
  }

  toggleMasterExpanded(): void {
    this.masterExpanded = !this.masterExpanded;
    if (this.masterExpanded) {
      // Reset flag so values are refreshed when panel opens
      this.masterEditValuesInitialized = false;
      this.loadMasterInfo();
    }
  }

  saveMasterFrequency(): void {
    this.savingMasterConfig = true;
    this.systemService.updateSystem('', { frequency: this.masterFrequency }).subscribe({
      next: () => {
        this.savingMasterConfig = false;
        this.messageService.add({
          severity: 'success',
          summary: 'Success',
          detail: `Master frequency updated to ${this.masterFrequency} MHz`
        });
      },
      error: () => {
        this.savingMasterConfig = false;
        this.messageService.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to update master frequency'
        });
      }
    });
  }

  saveMasterVoltage(): void {
    this.savingMasterConfig = true;
    this.systemService.updateSystem('', { coreVoltage: this.masterVoltage }).subscribe({
      next: () => {
        this.savingMasterConfig = false;
        this.messageService.add({
          severity: 'success',
          summary: 'Success',
          detail: `Master voltage updated to ${this.masterVoltage} mV`
        });
      },
      error: () => {
        this.savingMasterConfig = false;
        this.messageService.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to update master voltage'
        });
      }
    });
  }

  saveMasterFanSpeed(): void {
    this.savingMasterConfig = true;
    // Set manual fan speed and disable auto mode
    this.systemService.updateSystem('', { manualFanSpeed: this.masterFanSpeed, autofanspeed: 0 }).subscribe({
      next: () => {
        this.savingMasterConfig = false;
        this.masterFanMode = 1;  // Update local state to manual mode
        this.messageService.add({
          severity: 'success',
          summary: 'Success',
          detail: `Master fan speed updated to ${this.masterFanSpeed}%`
        });
      },
      error: () => {
        this.savingMasterConfig = false;
        this.messageService.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to update master fan speed'
        });
      }
    });
  }

  saveMasterFanMode(): void {
    this.savingMasterConfig = true;
    this.systemService.updateSystem('', { autofanspeed: this.masterFanMode === 0 ? 1 : 0 }).subscribe({
      next: () => {
        this.savingMasterConfig = false;
        this.messageService.add({
          severity: 'success',
          summary: 'Success',
          detail: `Master fan mode updated to ${this.masterFanMode === 0 ? 'Auto' : 'Manual'}`
        });
      },
      error: () => {
        this.savingMasterConfig = false;
        this.messageService.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to update master fan mode'
        });
      }
    });
  }

  // ========================================================================
  // Watchdog Methods
  // ========================================================================

  private startWatchdogPolling(): void {
    if (this.watchdogSubscription) return; // Already polling

    // Delay initial request slightly to let other requests complete first
    this.watchdogSubscription = interval(5000).pipe(
      startWith(0),
      switchMap(() => this.clusterService.getWatchdogStatus().pipe(
        catchError(err => {
          console.warn('Watchdog status fetch failed:', err);
          return of(null);
        })
      ))
    ).subscribe(status => {
      if (status) {
        this.watchdogStatus = status;
      }
    });
  }

  private stopWatchdogPolling(): void {
    if (this.watchdogSubscription) {
      this.watchdogSubscription.unsubscribe();
      this.watchdogSubscription = null;
    }
  }

  toggleWatchdog(): void {
    if (!this.watchdogStatus) return;

    this.watchdogToggling = true;
    const newState = !this.watchdogStatus.enabled;

    this.clusterService.setWatchdogEnabled('', newState).subscribe({
      next: () => {
        this.watchdogToggling = false;
        if (this.watchdogStatus) {
          this.watchdogStatus.enabled = newState;
        }
        this.messageService.add({
          severity: 'success',
          summary: 'Success',
          detail: `Watchdog ${newState ? 'enabled' : 'disabled'}`
        });
      },
      error: () => {
        this.watchdogToggling = false;
        this.messageService.add({
          severity: 'error',
          summary: 'Error',
          detail: 'Failed to toggle watchdog'
        });
      }
    });
  }

  getThrottleReasonLabel(reason: number): string {
    const reasons = [];
    if (reason & 1) reasons.push('High Temp');
    if (reason & 2) reasons.push('Low Vin');
    return reasons.length > 0 ? reasons.join(', ') : 'None';
  }

  // ========================================================================
  // Benchmark Methods
  // ========================================================================

  openBenchmarkDialog(targetIp: string = '', targetName: string = 'Master'): void {
    this.benchmarkTargetIp = targetIp;
    this.benchmarkTargetName = targetName;
    this.benchmarkConfig = {
      ...this.benchmarkService.getDefaultConfig(),
      targetIp,
      targetName
    };
    this.benchmarkLogs = [];
    this.showBenchmarkDialog = true;

    // Subscribe to logs
    if (this.benchmarkLogSubscription) {
      this.benchmarkLogSubscription.unsubscribe();
    }
    this.benchmarkLogSubscription = this.benchmarkService.getLogs().subscribe(log => {
      this.benchmarkLogs.push(log);
      // Keep only last 100 logs
      if (this.benchmarkLogs.length > 100) {
        this.benchmarkLogs.shift();
      }
    });
  }

  closeBenchmarkDialog(): void {
    this.showBenchmarkDialog = false;
    if (this.benchmarkLogSubscription) {
      this.benchmarkLogSubscription.unsubscribe();
      this.benchmarkLogSubscription = null;
    }
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
      await this.benchmarkService.applyBestResult(this.benchmarkTargetIp);
      this.messageService.add({
        severity: 'success',
        summary: 'Success',
        detail: 'Best benchmark settings applied'
      });
    } catch (e: any) {
      this.messageService.add({
        severity: 'error',
        summary: 'Error',
        detail: e.message || 'Failed to apply settings'
      });
    }
  }

  // ========================================================================
  // Utility Methods
  // ========================================================================

  /**
   * Get heatmap color for slave hashrate register display
   * Based on how the domain hashrate compares to expected (totalHashrate / numDomains)
   */
  getSlaveHeatmapColor(domainHashrate: number, totalHashrate: number): string {
    if (!totalHashrate || totalHashrate <= 0) {
      return 'var(--surface-border)';
    }

    // Get the primary color from CSS variable
    const primaryColor = getComputedStyle(document.documentElement).getPropertyValue('--primary-color').trim();

    // Convert hex to RGB
    const hex = primaryColor.replace('#', '');
    const r = parseInt(hex.substring(0, 2), 16);
    const g = parseInt(hex.substring(2, 4), 16);
    const b = parseInt(hex.substring(4, 6), 16);

    // Calculate expected hashrate per domain (rough estimate)
    // totalHashrate is in GH/s * 100, domain values are also scaled similarly
    const expectedPerDomain = totalHashrate / 4;  // Assume 4 domains typically
    const ratio = domainHashrate / expectedPerDomain;

    // Clamp ratio to 0-2 range and calculate deviation from 1
    const clampedRatio = Math.max(0, Math.min(2, ratio));
    const deviation = Math.abs(clampedRatio - 1);
    const t = 1 - Math.pow(1 - deviation, 3);
    const target = clampedRatio > 1 ? 255 : 0;

    // Interpolate color
    const finalR = (r * (1 - t) + target * t) | 0;
    const finalG = (g * (1 - t) + target * t) | 0;
    const finalB = (b * (1 - t) + target * t) | 0;

    return `rgb(${finalR}, ${finalG}, ${finalB})`;
  }
}
