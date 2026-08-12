import { Component, OnInit } from '@angular/core';
import { Observable, shareReplay } from 'rxjs';
import { SystemService } from '../services/system.service';
import { ClusterService } from '../services/cluster.service';
import { LayoutService } from './service/app.layout.service';
import { ISystemInfo } from 'src/models/ISystemInfo';

@Component({
  selector: 'app-menu',
  templateUrl: './app.menu.component.html'
})
export class AppMenuComponent implements OnInit {
  public info$!: Observable<ISystemInfo>;

  model: any[] = [];

  constructor(public layoutService: LayoutService,
    private systemService: SystemService,
    private clusterService: ClusterService,
  ) {
    this.info$ = this.systemService.getInfo().pipe(shareReplay({ refCount: true, bufferSize: 1 }))
  }

  ngOnInit() {
    // Build a provisional menu (standalone/Swarm) immediately, then swap to the
    // Cluster manager once we learn the device is running in master/slave mode.
    this.buildMenu(false);
    this.clusterService.getStatus().subscribe({
      next: (status) => this.buildMenu(status?.mode === 1 || status?.mode === 2),
      error: () => this.buildMenu(false),
    });
  }

  private buildMenu(clusterMode: boolean) {
    // Cluster (master/slave) shows the Cluster manager; standalone shows Swarm.
    const clusterOrSwarm = clusterMode
      ? { label: 'Cluster', icon: 'pi pi-fw pi-share-alt', routerLink: ['cluster'] }
      : { label: 'Swarm', icon: 'pi pi-fw pi-share-alt', routerLink: ['swarm'] };

    this.model = [
      {
        label: 'Menu',
        items: [
          { label: 'Dashboard', icon: 'pi pi-fw pi-home', routerLink: ['/'] },
          clusterOrSwarm,
          { label: 'Tuner', icon: 'pi pi-fw pi-sliders-h', routerLink: ['tuner'] },
          { label: 'Logs', icon: 'pi pi-fw pi-list', routerLink: ['logs'] },
          { label: 'System', icon: 'pi pi-fw pi-wave-pulse', routerLink: ['system'] },
          { separator: true },

          { label: 'Pool', icon: 'pi pi-fw pi-server', routerLink: ['pool'] },
          { label: 'Network', icon: 'pi pi-fw pi-wifi', routerLink: ['network'] },
          { label: 'Theme', icon: 'pi pi-fw pi-palette', routerLink: ['design'] },
          { label: 'Settings', icon: 'pi pi-fw pi-cog', routerLink: ['settings'] },
          { label: 'Update', icon: 'pi pi-fw pi-sync', routerLink: ['update'] },
          { separator: true },

          { label: 'Whitepaper', icon: 'pi pi-fw pi-bitcoin', command: () => window.open('/bitcoin.pdf', '_blank') },
        ]
      }
    ];
  }
}
