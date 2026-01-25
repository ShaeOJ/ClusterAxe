import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { BehaviorSubject, Observable, Subject, firstValueFrom, timer } from 'rxjs';
import { timeout } from 'rxjs/operators';

export type BenchmarkMode = 'efficiency' | 'overclock';

export interface BenchmarkConfig {
  mode: BenchmarkMode;       // 'efficiency' = best J/TH, 'overclock' = highest hashrate
  targetIp: string;          // IP address of device to benchmark (empty for master)
  targetName: string;        // Display name
  startVoltage: number;      // Starting voltage in mV (default: 1200)
  startFrequency: number;    // Starting frequency in MHz (default: 500)
  minVoltage: number;        // Minimum voltage (default: 1000)
  maxVoltage: number;        // Maximum voltage (default: 1400)
  minFrequency: number;      // Minimum frequency (default: 400)
  maxFrequency: number;      // Maximum frequency (default: 625, max: 999)
  voltageStep: number;       // Voltage increment (default: 20)
  frequencyStep: number;     // Frequency increment (default: 25)
  testDurationMs: number;    // Test duration per config (default: 600000 = 10 min)
  sampleIntervalMs: number;  // Sample interval (default: 15000 = 15 sec)
  stabilizationMs: number;   // Wait after restart (default: 90000 = 90 sec)
  maxTemp: number;           // Max chip temp (default: 66)
  maxVrTemp: number;         // Max VR temp (default: 86)
  maxPower: number;          // Max power watts (default: 40)
  minHashratePercent: number; // Min % of expected hashrate (default: 94)
}

export interface BenchmarkResult {
  voltage: number;
  frequency: number;
  avgHashrate: number;
  expectedHashrate: number;
  efficiency: number;        // J/TH
  avgTemp: number;
  avgPower: number;
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
  frequency: number;
  coreVoltage: number;
  hashRate: number;
  temp: number;
  vrTemp: number;
  power: number;
  voltage: number;
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
      // Overclock mode: higher limits, find max hashrate
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
        stabilizationMs: 15000,
        maxTemp: 70,             // Higher temp tolerance
        maxVrTemp: 90,
        maxPower: 50,            // Higher power tolerance
        minHashratePercent: 90   // Lower hashrate threshold
      };
    }
    // Efficiency mode: conservative limits, find best J/TH
    return {
      mode: 'efficiency',
      targetIp: '',
      targetName: 'Master',
      startVoltage: 1200,
      startFrequency: 500,
      minVoltage: 1100,
      maxVoltage: 1300,
      minFrequency: 400,
      maxFrequency: 625,
      voltageStep: 25,
      frequencyStep: 25,
      testDurationMs: 300000,
      sampleIntervalMs: 15000,
      stabilizationMs: 15000,
      maxTemp: 66,
      maxVrTemp: 86,
      maxPower: 40,
      minHashratePercent: 94
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
      this.log(`Device: ${deviceInfo.asicCount} ASICs, ${deviceInfo.smallCoreCount} cores each`);

      // Calculate expected hashrate formula: freq * cores * asics / 1000
      const calcExpectedHashrate = (freq: number) =>
        freq * deviceInfo.smallCoreCount * deviceInfo.asicCount / 1000;

      // Generate test configurations - systematic grid search
      const tests = this.generateTestConfigs(config);
      this.updateState({ totalTests: tests.length });
      this.log(`Testing ${tests.length} voltage/frequency combinations`);

      const maxTests = 50; // Safety limit to prevent runaway
      let testNum = 0;

      // Systematic search: try each combination
      for (const test of tests) {
        if (this.stopRequested) break;
        if (testNum >= maxTests) {
          this.log(`Reached max test limit (${maxTests}), stopping`);
          break;
        }

        testNum++;
        const { voltage, frequency } = test;

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
          }
        }

        this.updateState({ results: [...this.state.results] });
      }

      // Complete - auto-apply best result if found
      if (this.state.bestResult && !this.stopRequested) {
        this.log(`Benchmark complete! Applying best settings...`);
        this.log(`Best: ${this.state.bestResult.frequency} MHz @ ${this.state.bestResult.voltage} mV`);
        this.log(`Hashrate: ${this.state.bestResult.avgHashrate.toFixed(1)} GH/s, Efficiency: ${this.state.bestResult.efficiency.toFixed(2)} J/TH`);

        try {
          await this.applySettings(config.targetIp, this.state.bestResult.voltage, this.state.bestResult.frequency);
          this.log(`Applied best settings successfully!`);
        } catch (e: any) {
          this.log(`Failed to apply best settings: ${e.message}`);
        }
      } else if (!this.state.bestResult) {
        this.log('Benchmark complete - no valid configurations found');
      } else {
        this.log('Benchmark stopped by user');
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

    for (let v = config.startVoltage; v <= config.maxVoltage; v += config.voltageStep) {
      for (let f = config.startFrequency; f <= config.maxFrequency; f += config.frequencyStep) {
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
      smallCoreCount: response.smallCoreCount || response.coreCount || 894,
      asicCount: response.asicCount || 1,
      frequency: response.frequency || 500,
      coreVoltage: response.coreVoltage || response.coreVoltageActual || 1200,
      hashRate: response.hashRate || 0,
      temp: response.temp || 0,
      vrTemp: response.vrTemp || 0,
      power: response.power || 0,
      voltage: response.voltage || 5000
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

        // Safety checks
        if (info.temp > config.maxTemp) {
          this.log(`WARNING: Temp ${info.temp}°C exceeds max ${config.maxTemp}°C`);
        }
        if (info.vrTemp > config.maxVrTemp) {
          this.log(`WARNING: VR Temp ${info.vrTemp}°C exceeds max ${config.maxVrTemp}°C`);
        }
        if (info.power > config.maxPower) {
          this.log(`WARNING: Power ${info.power}W exceeds max ${config.maxPower}W`);
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
    if (samples.length < 5) {
      return {
        voltage, frequency, expectedHashrate,
        avgHashrate: 0, efficiency: 999, avgTemp: 0, avgPower: 0,
        samples: samples.length, passed: false, failReason: 'Insufficient samples'
      };
    }

    // Extract hashrates and sort
    const hashrates = samples.map(s => s.hashRate).sort((a, b) => a - b);
    const temps = samples.map(s => s.temp).sort((a, b) => a - b);
    const powers = samples.map(s => s.power).sort((a, b) => a - b);

    // Trim outliers (remove 2 lowest and 2 highest if enough samples)
    const trimCount = Math.min(2, Math.floor(hashrates.length / 4));
    const trimmedHashrates = hashrates.slice(trimCount, hashrates.length - trimCount);
    const trimmedTemps = temps.slice(trimCount, temps.length - trimCount);
    const trimmedPowers = powers.slice(trimCount, powers.length - trimCount);

    // Calculate averages
    const avgHashrate = trimmedHashrates.reduce((a, b) => a + b, 0) / trimmedHashrates.length;
    const avgTemp = trimmedTemps.reduce((a, b) => a + b, 0) / trimmedTemps.length;
    const avgPower = trimmedPowers.reduce((a, b) => a + b, 0) / trimmedPowers.length;

    // Calculate efficiency (J/TH = Watts / TH/s)
    const efficiency = avgPower / (avgHashrate / 1000);

    // Check pass/fail
    let passed = true;
    let failReason: string | undefined;

    const hashratePercent = (avgHashrate / expectedHashrate) * 100;
    if (hashratePercent < config.minHashratePercent) {
      passed = false;
      failReason = `Hashrate ${hashratePercent.toFixed(1)}% < ${config.minHashratePercent}%`;
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
      samples: samples.length, passed, failReason
    };
  }
}
