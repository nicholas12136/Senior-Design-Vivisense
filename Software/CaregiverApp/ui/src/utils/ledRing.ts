// LED ring geometry and SVG generation for ViviSense
// 93 LEDs in 6 rings (outer -> inner): [32, 24, 16, 12, 8, 1]

export const RING_COUNTS = [32, 24, 16, 12, 8, 1] as const;
export const LED_OFF = '#1a1a2e';

export const RED = '#ff2222';
export const ORANGE = '#ff8800';
export const YELLOW = '#ffdd00';
export const GREEN = '#00cc55';
export const BLUE = '#0066ff';

export type Sector = 'N' | 'NE' | 'E' | 'SE' | 'S' | 'SW' | 'W' | 'NW';
export const SECTOR_LABELS_4: Sector[] = ['N', 'E', 'S', 'W'];
export const SECTOR_LABELS_6: Sector[] = ['N', 'NE', 'SE', 'S', 'SW', 'NW'];
export const SECTOR_LABELS_8: Sector[] = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];
export const SECTOR_LABELS = SECTOR_LABELS_6;

export interface LEDPosition {
  x: number;
  y: number;
  ring: number;        // 0 = outermost, 5 = center
  indexInRing: number; // physical index order within ring (clockwise from bottom)
  angleDeg: number;    // signed angle: 0=forward/top, +right, -left, ±180=back
  sector: Sector;
}

export type LEDColorFn = (led: LEDPosition) => string;

function normalizeSignedAngle(angleDeg: number): number {
  let a = angleDeg;
  while (a >= 180) a -= 360;
  while (a < -180) a += 360;
  return a;
}

function getSector(angleDeg: number, labels: Sector[]): Sector {
  const n = labels.length;
  const step = 360 / n;
  let idx = Math.floor((angleDeg + step * 0.5) / step);
  idx %= n;
  if (idx < 0) idx += n;
  return labels[idx]!;
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

    const step = 360 / count;
    for (let i = 0; i < count; i++) {
      // i=0 is physical bottom. Index increases clockwise.
      const angleFromTopCw = ((180 + i * step) % 360 + 360) % 360;
      const signedAngle = normalizeSignedAngle(angleFromTopCw);
      const angleRad = (angleFromTopCw - 90) * (Math.PI / 180);
      const x = cx + r * Math.cos(angleRad);
      const y = cy + r * Math.sin(angleRad);
      leds.push({ x, y, ring, indexInRing: i, angleDeg: signedAngle, sector: getSector(signedAngle, sectorLabels) });
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
  const ringCount = RING_COUNTS.length;
  const radii = RING_COUNTS.map((_, i) => {
    if (i === ringCount - 1) return 0;
    const t = i / (ringCount - 2);
    return outerR * (1 - t * 0.86);
  });
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

export function ringColor(ring: number): string {
  if (ring >= 4) return RED;
  if (ring >= 2) return ORANGE;
  return YELLOW;
}

export function sectorModeColorFn(led: LEDPosition): string {
  if (led.ring === 5) return BLUE;
  if (led.sector === 'N') return RED;
  if (led.sector === 'SE') return ORANGE;
  if (led.sector === 'SW') return YELLOW;
  return GREEN;
}

export function layerModeColorFn(led: LEDPosition): string {
  if (led.ring === 5) return BLUE;
  if (led.sector === 'N' && led.ring >= 4) return RED;
  if (led.sector === 'SE' && led.ring >= 2 && led.ring <= 3) return ORANGE;
  if (led.sector === 'SW' && led.ring <= 1) return YELLOW;
  return LED_OFF;
}

export function radarModeColorFn(led: LEDPosition): string {
  if (led.ring === 5) return BLUE;
  if (led.ring === 4 && Math.abs(led.angleDeg) < 20) return RED;
  if (led.ring === 2 && led.angleDeg > 85 && led.angleDeg < 130) return ORANGE;
  if (led.ring === 0 && led.angleDeg < -90 && led.angleDeg > -130) return YELLOW;
  return LED_OFF;
}
