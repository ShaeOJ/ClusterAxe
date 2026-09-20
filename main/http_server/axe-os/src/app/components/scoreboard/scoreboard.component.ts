import { Component, Input, OnDestroy, OnInit } from '@angular/core';
import { Subscription, interval, startWith, switchMap } from 'rxjs';
import { ToastrService } from 'ngx-toastr';
import { IScoreboardEntry, SystemService } from 'src/app/services/system.service';

@Component({
  selector: 'app-scoreboard',
  templateUrl: './scoreboard.component.html',
})
export class ScoreboardComponent implements OnInit, OnDestroy {

  @Input() uri = '';

  public entries: IScoreboardEntry[] = [];
  public loading = true;

  private sub?: Subscription;

  constructor(
    private systemService: SystemService,
    private toastr: ToastrService,
  ) {}

  ngOnInit(): void {
    // Poll every 30s; the board only changes when a new best-diff share lands.
    this.sub = interval(30000).pipe(
      startWith(0),
      switchMap(() => this.systemService.getScoreboard(this.uri)),
    ).subscribe({
      next: (entries) => {
        this.entries = entries ?? [];
        this.loading = false;
      },
      error: (err) => {
        this.loading = false;
        this.toastr.error('Could not load the scoreboard', 'Error');
      },
    });
  }

  ngOnDestroy(): void {
    this.sub?.unsubscribe();
  }

  // Format a raw difficulty into a compact suffixed string (e.g. 12.3M).
  public formatDifficulty(diff: number): string {
    if (diff == null || isNaN(diff)) {
      return '—';
    }
    const units = ['', 'K', 'M', 'G', 'T', 'P', 'E'];
    let value = diff;
    let unit = 0;
    while (value >= 1000 && unit < units.length - 1) {
      value /= 1000;
      unit++;
    }
    return `${value.toFixed(value >= 100 || unit === 0 ? 0 : 2)}${units[unit]}`;
  }
}
