# BAP Cable Wiring Quick Reference

## Pinout

```
BAP Header (Looking at connector face)
┌─────────────┐
│ 1  2  3  4  │
│ ●  ●  ●  ●  │
└─────────────┘
  │  │  │  │
  │  │  │  └── Pin 4: GND (Ground)
  │  │  └───── Pin 3: RX (Receive)
  │  └──────── Pin 2: TX (Transmit)
  └─────────── Pin 1: 5V (Power - optional)
```

## Master ↔ Slave Wiring

```
MASTER          SLAVE
──────          ─────
Pin 1 (5V)  ─── Pin 1 (5V)    [OPTIONAL - only if powering slave from master]
Pin 2 (TX)  ─── Pin 3 (RX)    [REQUIRED - crossed]
Pin 3 (RX)  ─── Pin 2 (TX)    [REQUIRED - crossed]
Pin 4 (GND) ─── Pin 4 (GND)   [REQUIRED]
```

## Minimum 3-Wire Cable

For separate power supplies, you only need 3 wires:

```
MASTER          SLAVE
──────          ─────
Pin 2 (TX)  ─╲─ Pin 3 (RX)
              ╲
Pin 3 (RX)  ─╱─ Pin 2 (TX)

Pin 4 (GND) ─── Pin 4 (GND)
```

## Wire Color Suggestion

| Wire Color | Signal | Notes |
|------------|--------|-------|
| Red | 5V | Optional, leave unconnected if not needed |
| Green | TX→RX | Master Pin 2 to Slave Pin 3 |
| Yellow | RX←TX | Master Pin 3 to Slave Pin 2 |
| Black | GND | Always required |

## Cable Assembly

### Materials
- 4-pin JST-XH or Dupont connectors (2x)
- 24-26 AWG stranded wire
- Heat shrink tubing (optional)

### Steps
1. Cut 4 wires to desired length (max 30cm recommended)
2. Strip ~3mm from each end
3. Crimp or solder to connector pins
4. **Remember to cross TX/RX:**
   - Connector 1, Pin 2 → Connector 2, Pin 3
   - Connector 1, Pin 3 → Connector 2, Pin 2
5. Test continuity before connecting to devices

## Testing the Cable

Before connecting to Bitaxe devices:

1. Use multimeter in continuity mode
2. Verify:
   - Pin 2 on one end connects to Pin 3 on other end
   - Pin 3 on one end connects to Pin 2 on other end
   - Pin 4 connects to Pin 4 (GND to GND)
   - No shorts between any other pins

## Common Mistakes

| Mistake | Result | Fix |
|---------|--------|-----|
| TX-TX, RX-RX (straight) | No communication | Cross the TX/RX wires |
| Missing GND | No communication | Always connect GND |
| Cable too long (>50cm) | Intermittent errors | Use shorter cable or add buffer |
| Wrong pin order | Damage possible | Double-check pinout |

## Multi-Slave Wiring

### Daisy Chain (2-3 slaves max)
```
Master ──> Slave 1 ──> Slave 2
   TX→RX      TX→RX
   RX←TX      RX←TX
   GND────────GND──────GND
```

### Parallel (with hub)
```
              ┌──> Slave 1
Master ──> Hub├──> Slave 2
              └──> Slave 3
```

Use a UART repeater/hub for more than 3 slaves.
