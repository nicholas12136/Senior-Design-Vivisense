// LED Ring geometry and SVG generation for ViviSense
// 93 LEDs in 6 rings: [1, 6, 12, 18, 24, 32] (inner to outer)

export const RING_COUNTS = [1, 6, 12, 18, 24, 32] as const;
export const LED_OFF = '#1a1a2e';

// The only three proximity colors used across all modes
export const GREEN       = '#ffdd00'; // yellow — far/safe
export const YELLOW      = '#ff8800'; // orange — medium
export const RED         = '#ff2222'; // red    — close/danger
export const IDLE_GREEN  = '#00cc55'; // true green — default/background state

export type Sector = 'N' | 'NE' | 'E' | 'SE' | 'S' | 'SW' | 'W' | 'NW';
export const SECTOR_LABELS_4: Sector[] = ['N', 'E', 'S', 'W'];
export const SECTOR_LABELS_6: Sector[] = ['N', 'NE', 'SE', 'S', 'SW', 'NW'];
export const SECTOR_LABELS_8: Sector[] = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];
export const SECTOR_LABELS = SECTOR_LABELS_6; // kept for compatibility

export interface LEDPosition {
  x: number;
  y: number;
  ring: number;        // 0 = center/innermost, 5 = outermost
  indexInRing: number;
  angleDeg: number;    // clockwise from top (North); 0 for center LED
  sector: Sector;
}

export type LEDColorFn = (led: LEDPosition) => string;

function getSector(angleDeg: number, labels: Sector[]): Sector {
  const n = labels.length;
  const normalized = ((angleDeg % 360) + 360) % 360;
  const idx = Math.floor(((normalized + (360 / n / 2)) % 360) / (360 / n));
  return labels[idx % n]!;
}

export function generateLEDs(
  cx: number,
  cy: number,
  radii: number[],
  sectorLabels: Sector[] = SECTOR_LABELS_6,
): LEDPosition[] {
  const leds: LEDPosition[] = [];

  RING_COUNTS.forEach((count, ring) => {
    const r = radii[ring] ?? 0;

    if (count === 1) {
      leds.push({ x: cx, y: cy, ring, indexInRing: 0, angleDeg: 0, sector: sectorLabels[0]! });
      return;
    }

    for (let i = 0; i < count; i++) {
      // Offset the 6-LED ring by half a step (30°) so it has a LED at the east
      // and west cardinal positions (90°, 270°) rather than between them.
      const offset = count === 6 ? 30 : 0;
      const angleDeg = offset + (360 / count) * i;
      const angleRad = (angleDeg - 90) * (Math.PI / 180);
      const x = cx + r * Math.cos(angleRad);
      const y = cy + r * Math.sin(angleRad);
      leds.push({ x, y, ring, indexInRing: i, angleDeg, sector: getSector(angleDeg, sectorLabels) });
    }
  });

  return leds;
}

export function buildRingSVG(
  size: number,
  colorFn: LEDColorFn,
  brightness = 100,
  glow = true,
  sectorLabels: Sector[] = SECTOR_LABELS_6,
): string {
  const cx = size / 2;
  const cy = size / 2;
  const outerR = size / 2 * 0.87;
  const radii = RING_COUNTS.map((_, i) => (i === 0 ? 0 : (outerR / 5) * i));
  const ledR = Math.max(2.2, size * 0.018);
  const leds = generateLEDs(cx, cy, radii, sectorLabels);

  const circles = leds.map(led => {
    const color = colorFn(led);
    const isLit = color !== LED_OFF;
    const filter = isLit && glow ? 'filter="url(#ledglow)"' : '';
    return `<circle cx="${led.x.toFixed(2)}" cy="${led.y.toFixed(2)}" r="${ledR}" fill="${color}" ${filter}/>`;
  }).join('');

  const overlayOpacity = (1 - brightness / 100) * 0.85;
  const overlay = brightness < 100
    ? `<circle cx="${cx}" cy="${cy}" r="${outerR + 6}" fill="rgba(0,0,0,${overlayOpacity.toFixed(3)})" pointer-events="none"/>`
    : '';

  return `<svg width="${size}" height="${size}" viewBox="0 0 ${size} ${size}" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <filter id="ledglow" x="-60%" y="-60%" width="220%" height="220%">
      <feGaussianBlur stdDeviation="1.8" result="blur"/>
      <feMerge><feMergeNode in="blur"/><feMergeNode in="SourceGraphic"/></feMerge>
    </filter>
  </defs>
  <circle cx="${cx}" cy="${cy}" r="${outerR + 8}" fill="#0a0a14"/>
  <circle cx="${cx}" cy="${cy}" r="${outerR + 8}" fill="none" stroke="#22223a" stroke-width="1.5"/>
  ${circles}
  ${overlay}
</svg>`;
}

// ─── Ring color helper ────────────────────────────────────────────────────────

// Returns the proximity color for a given ring index (inner=red, middle=yellow, outer=green)
export function ringColor(ring: number): string {
  if (ring <= 1) return RED;
  if (ring <= 3) return YELLOW;
  return GREEN;
}

// ─── Built-in mode color functions ───────────────────────────────────────────

// Sector Mode preview: 3 evenly-spaced active sectors (N/SE/SW, ~120° apart), each a single color; remaining sectors idle green
export function sectorModeColorFn(led: LEDPosition): string {
  if (led.ring === 0) return LED_OFF;
  if (led.sector === 'N')  return RED;
  if (led.sector === 'SE') return YELLOW;
  if (led.sector === 'SW') return GREEN;
  return IDLE_GREEN;
}

// Layer Mode preview: same 3 sectors, each showing a different ring band
export function layerModeColorFn(led: LEDPosition): string {
  if (led.sector === 'N'  && led.ring <= 1) return RED;
  if (led.sector === 'SE' && led.ring >= 2 && led.ring <= 3) return YELLOW;
  if (led.sector === 'SW' && led.ring >= 4) return GREEN;
  return LED_OFF;
}

// Radar Mode: object clusters at varying positions/distances against a green background
export function radarModeColorFn(led: LEDPosition): string {
  if (led.ring === 0) return IDLE_GREEN;
  const a = led.angleDeg;
  const r = led.ring;

  // Close object to the north — larger red cluster across rings 1–2
  // Ring 1 (6-LED ring offset 30°) nearest-north LED is at 330°
  if (r === 1 && (a < 5 || a > 325)) return RED;
  if (r === 2 && (a < 35 || a > 325)) return RED;

  // Medium object to the southeast — orange cluster on ring 3
  if (r === 3 && a >= 100 && a <= 140) return YELLOW;

  // Distant object to the west — small yellow cluster on ring 5
  if (r === 5 && a > 250 && a < 295) return GREEN;

  return IDLE_GREEN;
}
