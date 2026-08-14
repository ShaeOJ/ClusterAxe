import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { BehaviorSubject, Observable, Subject, firstValueFrom, timer } from 'rxjs';
import { timeout } from 'rxjs/operators';

export type BenchmarkMode = 'efficiency' | 'overclock';

export interface BenchmarkConfig {
  mode: BenchmarkMode;        // 'efficiency' = best J/TH, 'overclock' = highest hashrate
  targetIp: string;           // IP address of device to benchmark (empty for master)
  targetName: string;         // Display name
  startVoltage: number;       // Starting voltage in mV
  startFrequency: number;     // Starting frequency in MHz
  minVoltage: number;         // Minimum voltage to test
  maxVoltage: number;         // Maximum voltage to test
  minFrequency: number;       // Minimum frequency to test
  maxFrequency: number;       // Maximum frequency to test
  voltageStep: number;        // Voltage increment per step
  frequencyStep: number;      // Frequency increment per step
  testDurationMs: number;     // Test duration per config
  sampleIntervalMs: number;   // Sample interval
  stabilizationMs: number;    // Wait after applying settings
  maxTemp: number;            // Abort if chip temp exceeds this
  maxVrTemp: number;          // Abort if VR temp exceeds this
  maxPower: number;           // Fail if average power exceeds this (W)
  minHashratePercent: number; // Fail if hashrate < this % of expected
  maxErrorPercent: number;    // Fail if hardware error rate exceeds this %
}

export interface BenchmarkResult {
  voltage: number;
  frequency: number;
  avgHashrate: number;
  expectedHashrate: number;
  efficiency: number;         // J/TH
  avgTemp: number;
  avgPower: number;
  avgErrorPercent: number;    // hardware error rate
  hashratePercent: number;    // % of expected hashrate achieved
  samples: number;
  passed: boolean;
  failReason?: string;
}

export interface BenchmarkState {
  running: boolean;
  phase: 'idle' | 'starting' | 'stabilizing' | 'sampling' | 'analyzing' | 'complete' | 'error';
  currentVoltage: number;
  currentFrequency: number;
  testNumber: number;
  totalTests: number;
  samplesCollected: number;
  samplesNeeded: number;
  timeRemainingMs: number;
  results: BenchmarkResult[];
  bestResult: BenchmarkResult | null;
  error: string | null;
  targetName: string;
}

export interface DeviceInfo {
  smallCoreCount: number;
  asicCount: number;
  frequency: number;          // current configured frequency (MHz)
  coreVoltage: number;
  hashRate: number;           // current hashrate (GH/s)
  expectedHashrate: number;   // firmware-calculated expected hashrate (GH/s) at current freq
  temp: number;
  vrTemp: number;
  power: number;
  voltage: number;
  errorPercentage: number;    // hardware error rate (%)
}

@Injectable({
  providedIn: 'root'
})
export class BenchmarkService {
  private state: BenchmarkState = this.getInitialState();
  private state$ = new BehaviorSubject<BenchmarkState>(this.state);
  private stopRequested = false;
  private log$ = new Subject<string>();

  constructor(private http: HttpClient) {}

  getState(): Observable<BenchmarkState> {
    return this.state$.asObservable();
  }

  getLogs(): Observable<string> {
    return this.log$.asObservable();
  }

  private getInitialState(): BenchmarkState {
    return {
      running: false,
      phase: 'idle',
      currentVoltage: 0,
      currentFrequency: 0,
      testNumber: 0,
      totalTests: 0,
      samplesCollected: 0,
      samplesNeeded: 0,
      timeRemainingMs: 0,
      results: [],
      bestResult: null,
      error: null,
      targetName: ''
    };
  }

  private updateState(partial: Partial<BenchmarkState>): void {
    this.state = { ...this.state, ...partial };
    this.state$.next(this.state);
  }

  private log(message: string): void {
    const timestamp = new Date().toLocaleTimeString();
    this.log$.next(`[${timestamp}] ${message}`);
    console.log(`[Benchmark] ${message}`);
  }

  getDefaultConfig(mode: BenchmarkMode = 'efficiency'): BenchmarkConfig {
    if (mode === 'overclock') {
      return {
        mode: 'overclock',
        targetIp: '',
        targetName: 'Master',
        startVoltage: 1200,
        startFrequency: 550,
        minVoltage: 1150,
        maxVoltage: 1400,
        minFrequency: 500,
        maxFrequency: 800,
        voltageStep: 25,
        frequencyStep: 25,
        testDurationMs: 300000,
        sampleIntervalMs: 15000,
        stabilizationMs: 30000,
        maxTemp: 70,
        maxVrTemp: 90,
        maxPower: 60,
        minHashratePercent: 85,  // Allow up to 15% below expected
        maxErrorPercent: 2.0     // Fail if hardware errors exceed 2%
      };
    }
    return {
      mode: 'efficiency',
      targetIp: '',
      targetName: 'Master',
      startVoltage: 1150,
      startFrequency: 500,
      minVoltage: 1050,
      maxVoltage: 1300,
      minFrequency: 450,
      maxFrequency: 700,
      voltageStep: 25,
      frequencyStep: 25,
      testDurationMs: 300000,
      sampleIntervalMs: 15000,
      stabilizationMs: 30000,
      maxTemp: 68,
      maxVrTemp: 86,
      maxPower: 50,
      minHashratePercent: 85,  // Allow up to 15% below expected
      maxErrorPercent: 1.5     // Fail if hardware errors exceed 1.5%
    };
  }

  async startBenchmark(config: BenchmarkConfig): Promise<void> {
    if (this.state.running) {
      throw new Error('Benchmark already running');
    }

    this.stopRequested = false;
    this.updateState({
      ...this.getInitialState(),
      running: true,
      phase: 'starting',
      targetName: config.targetName
    });

    const modeLabel = config.mode === 'overclock' ? 'OVERCLOCK (max hashrate)' : 'EFFICIENCY (best J/TH)';
    this.log(`Starting ${modeLabel} benchmark for ${config.targetName}`);

    try {
      // Get device info
      const deviceInfo = await this.getDeviceInfo(config.targetIp);
      this.log(`Device: ${deviceInfo.asicCount} ASIC(s) × ${deviceInfo.smallCoreCount} cores, ${deviceInfo.frequency} MHz, ${deviceInfo.coreVoltage} mV`);
      this.log(`Baseline: ${deviceInfo.expectedHashrate > 0 ? deviceInfo.expectedHashrate.toFixed(1) + ' GH/s expected' : 'no baseline'}, ${deviceInfo.errorPercentage.toFixed(2)}% hw errors`);

      // Scale the firmware's own expectedHashrate to any test frequency.
      // Falls back to theoretical formula if the API didn't provide one.
      const calcExpectedHashrate = (freq: number): number => {
        if (deviceInfo.expectedHashrate > 0 && deviceInfo.frequency > 0) {
          return (deviceInfo.expectedHashrate / deviceInfo.frequency) * freq;
        }
        return freq * deviceInfo.smallCoreCount * deviceInfo.asicCount / 1000;
      };

      // Generate test configurations - systematic grid search
      const tests = this.generateTestConfigs(config);
      this.updateState({ totalTests: tests.length });
      this.log(`Testing ${tests.length} voltage/frequency combinations`);

      const maxTests = 50; // Safety limit to prevent runaway
      let testNum = 0;

      // Smart skip tracking: skip remaining frequencies at a voltage if hashrate drops
      let prevVoltage: number | null = null;
      let prevHashrate: number | null = null;
      let prevPassed = false;
      let skipRemainingAtVoltage = false;
      let skippedCount = 0;

      // Systematic search: try each combination
      for (const test of tests) {
        if (this.stopRequested) break;
        if (testNum >= maxTests) {
          this.log(`Reached max test limit (${maxTests}), stopping`);
          break;
        }

        const { voltage, frequency } = test;

        // Reset tracking when voltage changes
        if (prevVoltage !== null && voltage !== prevVoltage) {
          prevHashrate = null;
          prevPassed = false;
          skipRemainingAtVoltage = false;
        }

        // Skip remaining frequencies at this voltage if hashrate already dropped
        if (skipRemainingAtVoltage && voltage === prevVoltage) {
          skippedCount++;
          this.log(`Skipping ${frequency} MHz @ ${voltage} mV (hashrate dropped at previous frequency)`);
          prevVoltage = voltage;
          continue;
        }

        testNum++;

        this.updateState({
          testNumber: testNum,
          totalTests: Math.min(tests.length, maxTests),
          currentVoltage: voltage,
          currentFrequency: frequency,
          phase: 'starting'
        });

        this.log(`Test ${testNum}/${Math.min(tests.length, maxTests)}: ${frequency} MHz @ ${voltage} mV`);

        // Apply settings
        try {
          await this.applySettings(config.targetIp, voltage, frequency);
        } catch (e: any) {
          this.log(`Failed to apply settings: ${e.message}, skipping`);
          continue;
        }

        // Wait for stabilization
        this.updateState({ phase: 'stabilizing' });
        this.log(`Stabilizing for ${config.stabilizationMs / 1000}s...`);
        await this.waitWithProgress(config.stabilizationMs);

        if (this.stopRequested) break;

        // Collect samples
        this.updateState({ phase: 'sampling', samplesCollected: 0 });
        const samplesNeeded = Math.floor(config.testDurationMs / config.sampleIntervalMs);
        this.updateState({ samplesNeeded });

        const samples = await this.collectSamples(config, samplesNeeded);

        if (this.stopRequested) break;

        // Analyze results
        this.updateState({ phase: 'analyzing' });
        const result = this.analyzeResults(samples, voltage, frequency, calcExpectedHashrate(frequency), config);
        this.state.results.push(result);

        const status = result.passed ? 'PASS' : `FAIL: ${result.failReason}`;
        this.log(`Result: ${result.avgHashrate.toFixed(1)} GH/s, ${result.efficiency.toFixed(2)} J/TH, ${result.avgTemp.toFixed(1)}°C - ${status}`);

        // Update best result based on mode
        if (result.passed) {
          let isBetter = false;
          if (config.mode === 'overclock') {
            // Overclock mode: highest hashrate wins
            isBetter = !this.state.bestResult || result.avgHashrate > this.state.bestResult.avgHashrate;
          } else {
            // Efficiency mode: lowest J/TH wins
            isBetter = !this.state.bestResult || result.efficiency < this.state.bestResult.efficiency;
          }

          if (isBetter) {
            this.updateState({ bestResult: result });
            if (config.mode === 'overclock') {
              this.log(`New best hashrate: ${frequency} MHz @ ${voltage} mV (${result.avgHashrate.toFixed(1)} GH/s)`);
            } else {
              this.log(`New best efficiency: ${frequency} MHz @ ${voltage} mV (${result.efficiency.toFixed(2)} J/TH)`);
            }
            // Persist best-so-far to NVS immediately. If the browser closes mid-run,
            // the device will boot with these settings on the next power cycle.
            try {
              await this.applySettings(config.targetIp, result.voltage, result.frequency);
              this.log(`Saved to NVS (boot-safe)`);
            } catch (_) { /* non-fatal — run continues */ }
          }
        }

        // Smart skip: check if hashrate dropped from previous test at same voltage
        if (prevHashrate !== null && prevPassed && result.avgHashrate < prevHashrate) {
          skipRemainingAtVoltage = true;
          this.log(`Hashrate dropped (${result.avgHashrate.toFixed(1)} < ${prevHashrate.toFixed(1)} GH/s) — skipping remaining frequencies at ${voltage} mV`);
        }
        prevVoltage = voltage;
        prevHashrate = result.avgHashrate;
        prevPassed = result.passed;

        this.updateState({ results: [...this.state.results] });
      }

      if (skippedCount > 0) {
        this.log(`Smart skip: skipped ${skippedCount} test(s) where hashrate was declining`);
      }

      // Apply best result on completion or stop — best-so-far is already in NVS from the
      // per-test apply above, but do a final explicit apply to confirm the device is on it.
      if (this.state.bestResult) {
        const reason = this.stopRequested ? 'stopped early' : 'complete';
        this.log(`Benchmark ${reason} — confirming best settings: ${this.state.bestResult.frequency} MHz @ ${this.state.bestResult.voltage} mV`);
        this.log(`Hashrate: ${this.state.bestResult.avgHashrate.toFixed(1)} GH/s, Efficiency: ${this.state.bestResult.efficiency.toFixed(2)} J/TH`);
        try {
          await this.applySettings(config.targetIp, this.state.bestResult.voltage, this.state.bestResult.frequency);
          this.log(`Best settings confirmed and saved`);
        } catch (e: any) {
          this.log(`Failed to confirm best settings: ${e.message}`);
        }
      } else {
        this.log(this.stopRequested ? 'Benchmark stopped — no valid result found' : 'Benchmark complete — no valid configurations found');
      }

      this.updateState({ phase: 'complete', running: false });

    } catch (error: any) {
      this.log(`Error: ${error.message}`);
      this.updateState({
        phase: 'error',
        running: false,
        error: error.message
      });
    }
  }

  stopBenchmark(): void {
    this.log('Stop requested');
    this.stopRequested = true;
  }

  async applyBestResult(targetIp: string): Promise<void> {
    if (!this.state.bestResult) {
      throw new Error('No best result to apply');
    }

    const { voltage, frequency } = this.state.bestResult;
    this.log(`Applying best settings: ${frequency} MHz @ ${voltage} mV`);
    await this.applySettings(targetIp, voltage, frequency);
    this.log('Settings applied successfully');
  }

  private generateTestConfigs(config: BenchmarkConfig): Array<{voltage: number, frequency: number}> {
    const tests: Array<{voltage: number, frequency: number}> = [];

    for (let v = config.minVoltage; v <= config.maxVoltage; v += config.voltageStep) {
      for (let f = config.minFrequency; f <= config.maxFrequency; f += config.frequencyStep) {
        tests.push({ voltage: v, frequency: f });
      }
    }

    return tests;
  }

  private async getDeviceInfo(targetIp: string): Promise<DeviceInfo> {
    const url = targetIp ? `http://${targetIp}/api/system/info` : '/api/system/info';
    const response = await firstValueFrom(
      this.http.get<any>(url).pipe(timeout(10000))
    );

    return {
      smallCoreCount: response.smallCoreCount || 894,
      asicCount: response.asicCount || 1,
      frequency: response.frequency || 500,
      coreVoltage: response.coreVoltageActual || response.coreVoltage || 1200,
      hashRate: response.hashRate || 0,
      expectedHashrate: response.expectedHashrate || 0,
      temp: response.temp || 0,
      vrTemp: response.vrTemp || 0,
      power: response.power || 0,
      voltage: response.voltage || 0,
      errorPercentage: response.errorPercentage || 0
    };
  }

  private async applySettings(targetIp: string, voltage: number, frequency: number): Promise<void> {
    const url = targetIp ? `http://${targetIp}/api/system` : '/api/system';

    await firstValueFrom(
      this.http.patch(url, { coreVoltage: voltage, frequency: frequency }).pipe(timeout(10000))
    );

    // No restart needed - Bitaxe applies settings immediately
  }

  private async waitWithProgress(ms: number): Promise<void> {
    const startTime = Date.now();
    const endTime = startTime + ms;

    while (Date.now() < endTime && !this.stopRequested) {
      this.updateState({ timeRemainingMs: endTime - Date.now() });
      await firstValueFrom(timer(1000));
    }
  }

  private async collectSamples(config: BenchmarkConfig, samplesNeeded: number): Promise<DeviceInfo[]> {
    const samples: DeviceInfo[] = [];

    for (let i = 0; i < samplesNeeded && !this.stopRequested; i++) {
      try {
        const info = await this.getDeviceInfo(config.targetIp);

        // Safety checks — abort sampling if temp is dangerous
        if (info.temp > config.maxTemp) {
          this.log(`ABORT: Temp ${info.temp.toFixed(1)}°C exceeds max ${config.maxTemp}°C`);
          break;
        }
        if (info.vrTemp > config.maxVrTemp) {
          this.log(`ABORT: VR Temp ${info.vrTemp.toFixed(1)}°C exceeds max ${config.maxVrTemp}°C`);
          break;
        }
        if (info.errorPercentage > config.maxErrorPercent * 3) {
          this.log(`ABORT: HW errors ${info.errorPercentage.toFixed(2)}% critically high`);
          break;
        }

        samples.push(info);
        this.updateState({
          samplesCollected: samples.length,
          timeRemainingMs: (samplesNeeded - samples.length) * config.sampleIntervalMs
        });

      } catch (e) {
        this.log(`Sample ${i + 1} failed, device may be restarting`);
      }

      if (i < samplesNeeded - 1 && !this.stopRequested) {
        await firstValueFrom(timer(config.sampleIntervalMs));
      }
    }

    return samples;
  }

  private analyzeResults(
    samples: DeviceInfo[],
    voltage: number,
    frequency: number,
    expectedHashrate: number,
    config: BenchmarkConfig
  ): BenchmarkResult {
    if (samples.length < 3) {
      return {
        voltage, frequency, expectedHashrate,
        avgHashrate: 0, efficiency: 999, avgTemp: 0, avgPower: 0,
        avgErrorPercent: 0, hashratePercent: 0,
        samples: samples.length, passed: false, failReason: 'Insufficient samples'
      };
    }

    const avg = (arr: number[]) => arr.length > 0 ? arr.reduce((a, b) => a + b, 0) / arr.length : 0;

    // Trim outliers (2 lowest + 2 highest if enough samples)
    const trim = <T>(arr: T[]): T[] => {
      const sorted = [...arr].sort((a: any, b: any) => a - b);
      const n = Math.min(2, Math.floor(sorted.length / 5));
      return sorted.slice(n, sorted.length - n);
    };

    const avgHashrate   = avg(trim(samples.map(s => s.hashRate)));
    const avgTemp       = avg(trim(samples.map(s => s.temp)));
    const avgPower      = avg(trim(samples.map(s => s.power)));
    const avgErrorPercent = avg(samples.map(s => s.errorPercentage)); // no trim — errors matter

    const efficiency = avgPower > 0 && avgHashrate > 0 ? avgPower / (avgHashrate / 1000) : 999;
    const hashratePercent = expectedHashrate > 0 ? (avgHashrate / expectedHashrate) * 100 : 100;

    let passed = true;
    let failReason: string | undefined;

    if (hashratePercent < config.minHashratePercent) {
      passed = false;
      failReason = `Hashrate ${hashratePercent.toFixed(1)}% of expected`;
    }
    if (avgErrorPercent > config.maxErrorPercent) {
      passed = false;
      failReason = `HW errors ${avgErrorPercent.toFixed(2)}% > ${config.maxErrorPercent}%`;
    }
    if (avgTemp > config.maxTemp) {
      passed = false;
      failReason = `Temp ${avgTemp.toFixed(1)}°C > ${config.maxTemp}°C`;
    }
    if (avgPower > config.maxPower) {
      passed = false;
      failReason = `Power ${avgPower.toFixed(1)}W > ${config.maxPower}W`;
    }

    return {
      voltage, frequency, expectedHashrate,
      avgHashrate, efficiency, avgTemp, avgPower,
      avgErrorPercent, hashratePercent,
      samples: samples.length, passed, failReason
    };
  }
}
