import { HttpClient } from '@angular/common/http';
import { Injectable } from '@angular/core';
import { Observable, of } from 'rxjs';
import { map } from 'rxjs/operators';

export interface IFirePoolWorker {
  hashrate: number;
  sharesPerSecond: number;
}

export interface IFirePoolPoolStats {
  coinSymbol: string;
  coinName: string;
  connectedMiners: number;
  poolHashrate: number;
  sharesPerSecond: number;
  networkHashrate: number;
  networkDifficulty: number;
  blockHeight: number;
  lastNetworkBlockTime: string;
  poolId: string;
}

export interface IFirePoolMinerStats {
  pendingBalance: number;
  totalPaid: number;
  workers: { [name: string]: IFirePoolWorker };
  totalWork: number;
  invalidShares: number;
  staleShares: number;
  bestShareDifficulty: number;
  lastShare: string;
  totalSharesSubmitted: number;
}

// Maps first port of each pool range to pool ID
const PORT_TO_POOL_ID: { [port: number]: string } = {
  // btc1 (Bitcoin, main pool)
  4333: 'btc1', 4334: 'btc1', 4337: 'btc1', 4338: 'btc1',
  4340: 'btc1', 4341: 'btc1', 4345: 'btc1',
  4339: 'btc1-solo',
  // btc2 (Bitcoin II)
  3333: 'btc2', 3334: 'btc2', 3337: 'btc2', 3338: 'btc2',
  3340: 'btc2', 3345: 'btc2',
  3339: 'btc2-solo',
  // bch1 (Bitcoin Cash)
  5333: 'bch1', 5334: 'bch1', 5337: 'bch1', 5338: 'bch1',
  5340: 'bch1', 5345: 'bch1',
  5339: 'bch1-solo',
  // dgb1 (DigiByte SHA256d)
  6333: 'dgb1', 6334: 'dgb1', 6337: 'dgb1', 6338: 'dgb1',
  6340: 'dgb1', 6345: 'dgb1',
  6339: 'dgb1-solo',
  // dgb-scrypt1
  6666: 'dgb-scrypt1',
  6669: 'dgb-scrypt1-solo',
  // btcs1 (Bitcoin Silver)
  7333: 'btcs1', 7345: 'btcs1',
  7339: 'btcs1-solo',
  // xec1 (eCash)
  9333: 'xec1',
  9339: 'xec1-solo',
  // ltc1 (Litecoin)
  2333: 'ltc1',
  2339: 'ltc1-solo',
  // bch2-1 (Bitcoin Cash II)
  1333: 'bch2-1',
  1339: 'bch2-1-solo',
  // ppc1 (Peercoin)
  12333: 'ppc1',
  12339: 'ppc1-solo',
  // fix1 (FIX)
  15333: 'fix1',
  15339: 'fix1-solo',
};

const FIREPOOL_API = 'https://firepool.ca/api';

@Injectable({
  providedIn: 'root'
})
export class FirePoolService {

  constructor(private httpClient: HttpClient) {}

  isFirePool(stratumURL: string): boolean {
    return stratumURL.includes('firepool.ca') || stratumURL.includes('10.0.0.20');
  }

  getPoolId(port: number): string | null {
    return PORT_TO_POOL_ID[port] ?? null;
  }

  getMinerAddress(stratumUser: string): string {
    return stratumUser.split('.')[0];
  }

  getPoolStats(poolId: string): Observable<IFirePoolPoolStats> {
    return this.httpClient.get<any>(`${FIREPOOL_API}/pools/${poolId}`).pipe(
      map(res => {
        const p = res.pool ?? res;
        return {
          poolId,
          coinSymbol: p.coin?.symbol ?? '',
          coinName: p.coin?.name ?? '',
          connectedMiners: p.poolStats?.connectedMiners ?? 0,
          poolHashrate: p.poolStats?.poolHashrate ?? 0,
          sharesPerSecond: p.poolStats?.sharesPerSecond ?? 0,
          networkHashrate: p.networkStats?.networkHashrate ?? 0,
          networkDifficulty: p.networkStats?.networkDifficulty ?? 0,
          blockHeight: p.networkStats?.blockHeight ?? 0,
          lastNetworkBlockTime: p.networkStats?.lastNetworkBlockTime ?? '',
        } as IFirePoolPoolStats;
      })
    );
  }

  getMinerStats(poolId: string, address: string): Observable<IFirePoolMinerStats> {
    return this.httpClient.get<any>(`${FIREPOOL_API}/pools/${poolId}/miners/${address}`).pipe(
      map(res => ({
        pendingBalance: res.pendingBalance ?? 0,
        totalPaid: res.totalPaid ?? 0,
        workers: res.performance?.workers ?? {},
        totalWork: res.totalWork ?? 0,
        invalidShares: res.invalidShares ?? 0,
        staleShares: res.staleShares ?? 0,
        bestShareDifficulty: res.bestShareDifficulty ?? 0,
        lastShare: res.lastShare ?? '',
        totalSharesSubmitted: res.totalSharesSubmitted ?? 0,
      } as IFirePoolMinerStats))
    );
  }
}
