import { Component, OnInit, OnDestroy } from '@angular/core';
import { Subject } from 'rxjs';
import { takeUntil } from 'rxjs/operators';
import { LayoutService } from '../../layout/service/app.layout.service';
import { ThemeService } from '../../services/theme.service';

interface ThemeOption {
  name: string;
  primaryColor: string;
  accentColors: {
    [key: string]: string;
  };
}

@Component({
  selector: 'app-theme-config',
  template: `
    <div class="card">
      <div class="grid">
        <div class="col-12">
          <h5>Color Scheme</h5>
          <div class="flex gap-3">
            <div class="flex align-items-center">
              <p-radioButton name="colorScheme" [value]="'dark'" [(ngModel)]="selectedScheme"
                (onClick)="changeColorScheme('dark')" inputId="dark"></p-radioButton>
              <label for="dark" class="ml-2">Dark</label>
            </div>
            <div class="flex align-items-center">
              <p-radioButton name="colorScheme" [value]="'light'" [(ngModel)]="selectedScheme"
                (onClick)="changeColorScheme('light')" inputId="light"></p-radioButton>
              <label for="light" class="ml-2">Light</label>
            </div>
          </div>
        </div>

        <div class="col-12 mt-4">
          <h5>Theme Colors</h5>
          <div class="grid gap-2">
            <div *ngFor="let theme of themes" class="col-2 theme-color">
              <button pButton [class]="'p-button-rounded p-button-text color-dot'"
                      [style.backgroundColor]="theme.primaryColor"
                      style="width: 2rem; height: 2rem; border: none;"
                      (click)="changeTheme(theme)">
                <i *ngIf="theme.primaryColor === currentColor" class="pi pi-check selected-icon"></i>
              </button>
              <div class="text-sm mt-1">{{theme.name}}</div>
            </div>
          </div>
        </div>
      </div>
    </div>
  `,
  styleUrls: ['./design-component.scss']
})
export class ThemeConfigComponent implements OnInit {
  selectedScheme: string;
  currentColor: string = '';
  // All Pip-Boy themed accent colors
  themes: ThemeOption[] = [
    {
      name: 'Green',
      primaryColor: '#14F593',
      accentColors: {
        '--primary-color': '#14F593',
        '--primary-color-text': '#0A0F0D',
        '--highlight-bg': 'rgba(20, 245, 147, 0.16)',
        '--highlight-text-color': '#E8F4F0',
        '--focus-ring': '0 0 0 0.2rem rgba(20, 245, 147, 0.3)',
        '--slider-bg': '#1a3329',
        '--slider-range-bg': '#14F593',
        '--slider-handle-bg': '#14F593',
        '--progressbar-bg': '#1a3329',
        '--progressbar-value-bg': '#14F593',
        '--checkbox-border': '#14F593',
        '--checkbox-bg': '#14F593',
        '--checkbox-hover-bg': '#10c476',
        '--button-bg': '#14F593',
        '--button-hover-bg': '#10c476',
        '--button-focus-shadow': '0 0 0 2px #0A0F0D, 0 0 0 4px #14F593',
        '--togglebutton-bg': '#14F593',
        '--togglebutton-border': '1px solid #14F593',
        '--togglebutton-hover-bg': '#10c476',
        '--togglebutton-hover-border': '1px solid #10c476',
        '--togglebutton-text-color': '#0A0F0D'
      }
    },
    {
      name: 'Amber',
      primaryColor: '#FFB000',
      accentColors: {
        '--primary-color': '#FFB000',
        '--primary-color-text': '#0A0F0D',
        '--highlight-bg': 'rgba(255, 176, 0, 0.16)',
        '--highlight-text-color': '#E8F4F0',
        '--focus-ring': '0 0 0 0.2rem rgba(255, 176, 0, 0.3)',
        '--slider-bg': '#2a2a1a',
        '--slider-range-bg': '#FFB000',
        '--slider-handle-bg': '#FFB000',
        '--progressbar-bg': '#2a2a1a',
        '--progressbar-value-bg': '#FFB000',
        '--checkbox-border': '#FFB000',
        '--checkbox-bg': '#FFB000',
        '--checkbox-hover-bg': '#e69e00',
        '--button-bg': '#FFB000',
        '--button-hover-bg': '#e69e00',
        '--button-focus-shadow': '0 0 0 2px #0A0F0D, 0 0 0 4px #FFB000',
        '--togglebutton-bg': '#FFB000',
        '--togglebutton-border': '1px solid #FFB000',
        '--togglebutton-hover-bg': '#e69e00',
        '--togglebutton-hover-border': '1px solid #e69e00',
        '--togglebutton-text-color': '#0A0F0D'
      }
    },
    {
      name: 'Blue',
      primaryColor: '#00D4FF',
      accentColors: {
        '--primary-color': '#00D4FF',
        '--primary-color-text': '#0A0F0D',
        '--highlight-bg': 'rgba(0, 212, 255, 0.16)',
        '--highlight-text-color': '#E8F4F0',
        '--focus-ring': '0 0 0 0.2rem rgba(0, 212, 255, 0.3)',
        '--slider-bg': '#1a2a33',
        '--slider-range-bg': '#00D4FF',
        '--slider-handle-bg': '#00D4FF',
        '--progressbar-bg': '#1a2a33',
        '--progressbar-value-bg': '#00D4FF',
        '--checkbox-border': '#00D4FF',
        '--checkbox-bg': '#00D4FF',
        '--checkbox-hover-bg': '#00b8e6',
        '--button-bg': '#00D4FF',
        '--button-hover-bg': '#00b8e6',
        '--button-focus-shadow': '0 0 0 2px #0A0F0D, 0 0 0 4px #00D4FF',
        '--togglebutton-bg': '#00D4FF',
        '--togglebutton-border': '1px solid #00D4FF',
        '--togglebutton-hover-bg': '#00b8e6',
        '--togglebutton-hover-border': '1px solid #00b8e6',
        '--togglebutton-text-color': '#0A0F0D'
      }
    },
    {
      name: 'Orange',
      primaryColor: '#FF8C00',
      accentColors: {
        '--primary-color': '#FF8C00',
        '--primary-color-text': '#0A0F0D',
        '--highlight-bg': 'rgba(255, 140, 0, 0.16)',
        '--highlight-text-color': '#E8F4F0',
        '--focus-ring': '0 0 0 0.2rem rgba(255, 140, 0, 0.3)',
        '--slider-bg': '#2a2a1a',
        '--slider-range-bg': '#FF8C00',
        '--slider-handle-bg': '#FF8C00',
        '--progressbar-bg': '#2a2a1a',
        '--progressbar-value-bg': '#FF8C00',
        '--checkbox-border': '#FF8C00',
        '--checkbox-bg': '#FF8C00',
        '--checkbox-hover-bg': '#e67e00',
        '--button-bg': '#FF8C00',
        '--button-hover-bg': '#e67e00',
        '--button-focus-shadow': '0 0 0 2px #0A0F0D, 0 0 0 4px #FF8C00',
        '--togglebutton-bg': '#FF8C00',
        '--togglebutton-border': '1px solid #FF8C00',
        '--togglebutton-hover-bg': '#e67e00',
        '--togglebutton-hover-border': '1px solid #e67e00',
        '--togglebutton-text-color': '#0A0F0D'
      }
    },
    {
      name: 'Pink',
      primaryColor: '#FF6EC7',
      accentColors: {
        '--primary-color': '#FF6EC7',
        '--primary-color-text': '#0A0F0D',
        '--highlight-bg': 'rgba(255, 110, 199, 0.16)',
        '--highlight-text-color': '#E8F4F0',
        '--focus-ring': '0 0 0 0.2rem rgba(255, 110, 199, 0.3)',
        '--slider-bg': '#2a1a26',
        '--slider-range-bg': '#FF6EC7',
        '--slider-handle-bg': '#FF6EC7',
        '--progressbar-bg': '#2a1a26',
        '--progressbar-value-bg': '#FF6EC7',
        '--checkbox-border': '#FF6EC7',
        '--checkbox-bg': '#FF6EC7',
        '--checkbox-hover-bg': '#e660b3',
        '--button-bg': '#FF6EC7',
        '--button-hover-bg': '#e660b3',
        '--button-focus-shadow': '0 0 0 2px #0A0F0D, 0 0 0 4px #FF6EC7',
        '--togglebutton-bg': '#FF6EC7',
        '--togglebutton-border': '1px solid #FF6EC7',
        '--togglebutton-hover-bg': '#e660b3',
        '--togglebutton-hover-border': '1px solid #e660b3',
        '--togglebutton-text-color': '#0A0F0D'
      }
    },
    {
      name: 'Red',
      primaryColor: '#FF3F3F',
      accentColors: {
        '--primary-color': '#FF3F3F',
        '--primary-color-text': '#0A0F0D',
        '--highlight-bg': 'rgba(255, 63, 63, 0.16)',
        '--highlight-text-color': '#E8F4F0',
        '--focus-ring': '0 0 0 0.2rem rgba(255, 63, 63, 0.3)',
        '--slider-bg': '#2a1a1a',
        '--slider-range-bg': '#FF3F3F',
        '--slider-handle-bg': '#FF3F3F',
        '--progressbar-bg': '#2a1a1a',
        '--progressbar-value-bg': '#FF3F3F',
        '--checkbox-border': '#FF3F3F',
        '--checkbox-bg': '#FF3F3F',
        '--checkbox-hover-bg': '#e63636',
        '--button-bg': '#FF3F3F',
        '--button-hover-bg': '#e63636',
        '--button-focus-shadow': '0 0 0 2px #0A0F0D, 0 0 0 4px #FF3F3F',
        '--togglebutton-bg': '#FF3F3F',
        '--togglebutton-border': '1px solid #FF3F3F',
        '--togglebutton-hover-bg': '#e63636',
        '--togglebutton-hover-border': '1px solid #e63636',
        '--togglebutton-text-color': '#0A0F0D'
      }
    }
  ];

  private destroy$ = new Subject<void>();

  constructor(
    public layoutService: LayoutService,
    private themeService: ThemeService
  ) {
    this.selectedScheme = this.layoutService.config().colorScheme;
  }

  ngOnInit() {
    this.themeService.getThemeSettings()
      .pipe(takeUntil(this.destroy$))
      .subscribe({
        next: (settings) => {
          if (settings) {
            if (settings.colorScheme) {
              this.selectedScheme = settings.colorScheme;
            }
            if (settings.accentColors) {
              this.applyThemeColors(settings.accentColors);
              this.currentColor = settings.accentColors['--primary-color'];
            }
          }
        },
        error: (error) => console.error('Error loading theme settings:', error)
      });
  }

  ngOnDestroy() {
    this.destroy$.next();
    this.destroy$.complete();
  }

  private applyThemeColors(colors: { [key: string]: string }) {
    Object.entries(colors).forEach(([key, value]) => {
      document.documentElement.style.setProperty(key, value);
    });
  }

  changeColorScheme(scheme: string) {
    this.selectedScheme = scheme;
    const config = { ...this.layoutService.config() };
    config.colorScheme = scheme;
    this.layoutService.config.set(config);

    this.themeService.saveThemeSettings({ colorScheme: scheme })
      .pipe(takeUntil(this.destroy$))
      .subscribe({
        error: (error) => console.error('Error saving theme settings:', error)
      });
  }

  changeTheme(theme: ThemeOption) {
    this.applyThemeColors(theme.accentColors);
    this.currentColor = theme.primaryColor;

    this.themeService.saveThemeSettings({
      colorScheme: this.selectedScheme,
      accentColors: theme.accentColors
    }).pipe(takeUntil(this.destroy$))
      .subscribe({
        error: (error) => console.error('Error saving theme settings:', error)
      });
  }
}
