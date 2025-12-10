import { Component, OnInit, OnDestroy } from '@angular/core';

@Component({
  selector: 'app-root',
  templateUrl: './app.component.html',
  styleUrls: ['./app.component.scss']
})
export class AppComponent implements OnInit, OnDestroy {
  private glitchInterval: any;

  constructor() {}

  ngOnInit() {
    // Start random glitch effect system
    this.startRandomGlitches();
  }

  ngOnDestroy() {
    if (this.glitchInterval) {
      clearInterval(this.glitchInterval);
    }
  }

  private startRandomGlitches() {
    // Check every 2-5 seconds for a potential glitch
    this.glitchInterval = setInterval(() => {
      // 8% chance of glitch occurring each interval
      if (Math.random() < 0.08) {
        this.triggerRandomGlitch();
      }
    }, 2000 + Math.random() * 3000);
  }

  private triggerRandomGlitch() {
    const glitchType = Math.floor(Math.random() * 3);

    switch (glitchType) {
      case 0:
        this.triggerScreenGlitch();
        break;
      case 1:
        this.triggerCRTFlicker();
        break;
      case 2:
        this.triggerTextGlitch();
        break;
    }
  }

  private triggerScreenGlitch() {
    // Create overlay element for screen glitch
    const overlay = document.createElement('div');
    overlay.className = 'glitch-overlay';
    document.body.appendChild(overlay);

    // Remove after animation completes
    setTimeout(() => {
      overlay.remove();
    }, 200);
  }

  private triggerCRTFlicker() {
    // Add flicker class to body
    document.body.classList.add('crt-flicker');

    // Remove after animation
    setTimeout(() => {
      document.body.classList.remove('crt-flicker');
    }, 150);
  }

  private triggerTextGlitch() {
    // Find random heading or stat element to glitch
    const elements = document.querySelectorAll('h1, h2, h3, h4, .stat-value, .hashrate');
    if (elements.length === 0) return;

    const randomEl = elements[Math.floor(Math.random() * elements.length)] as HTMLElement;
    const originalText = randomEl.textContent || '';

    // Store original text and add glitch class
    randomEl.setAttribute('data-text', originalText);
    randomEl.classList.add('glitch-text');

    // Remove glitch after animation
    setTimeout(() => {
      randomEl.classList.remove('glitch-text');
    }, 300);
  }
}
