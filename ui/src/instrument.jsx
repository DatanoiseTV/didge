/* ============================================================
   Didge · InstrumentView — the live cutaway
   ============================================================
   A side-view section through the instrument: vocal tract on the
   left, lips, then the bore opening out to the bell on the right.

   Everything that moves is driven by the `levels` event and
   painted from a single rAF loop that mutates SVG attributes in
   place. The 30 Hz telemetry never touches React state — a
   re-render per event would repaint the whole panel tree and
   fight the animation.

   The bore outline is drawn from the engine's own 16 segment
   radii, so what you see is the geometry the waveguide is
   actually running; the drag zones write back to the two
   parameters that shape it.
   ============================================================ */

/* ---- drawing frame (SVG user units) ---- */
/* Aspect matches the card it sits in, so the drawing fills the panel instead
   of letterboxing inside it. */
const VB_W = 1280, VB_H = 306, CY = 153;
const TRACT_X0 = 58, TRACT_X1 = 200;   // glottis -> mouth
const LIP_X = 214, LIP_W = 32;
const BORE_X0 = 252, BORE_X1 = 1128;
const BORE_SPAN = BORE_X1 - BORE_X0;
/* Bore radii arrive in metres. The scale is fixed rather than fitted to the
   current bore, so growing the bell actually grows the drawing. It is set so a
   typical instrument keeps roughly its real proportions — a didgeridoo is
   about eight times longer than its bell is wide, and a scale that filled the
   frame vertically drew a squat cone instead. */
const M_PX = 1500;
const CM_PX = 26;                      // tract radii are centimetres
const WALL = 9;                        // drawn wall thickness
const MAX_HALF = VB_H / 2 - 22;

/* Boundaries of the two draggable regions, as a fraction of bore length. */
const FLARE_ZONE = [0.20, 0.68];
const BELL_ZONE = [0.68, 1.0];

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
function hzToNote(hz) {
  if (!(hz > 0)) return '—';
  const m = Math.round(69 + 12 * Math.log2(hz / 440));
  return NOTE_NAMES[((m % 12) + 12) % 12] + (Math.floor(m / 12) - 1);
}

/* Catmull-Rom through the segment samples, emitted as cubic beziers — the
   bore is only sampled 16 times and a polyline reads as a faceted cone. */
function smoothPath(pts, lead) {
  if (!pts.length) return '';
  let d = (lead === false ? ' L ' : 'M ') + pts[0][0].toFixed(1) + ' ' + pts[0][1].toFixed(1);
  for (let i = 0; i < pts.length - 1; i++) {
    const p0 = pts[i - 1] || pts[i], p1 = pts[i], p2 = pts[i + 1], p3 = pts[i + 2] || pts[i + 1];
    const c1x = p1[0] + (p2[0] - p0[0]) / 6, c1y = p1[1] + (p2[1] - p0[1]) / 6;
    const c2x = p2[0] - (p3[0] - p1[0]) / 6, c2y = p2[1] - (p3[1] - p1[1]) / 6;
    d += ` C ${c1x.toFixed(1)} ${c1y.toFixed(1)} ${c2x.toFixed(1)} ${c2y.toFixed(1)} ${p2[0].toFixed(1)} ${p2[1].toFixed(1)}`;
  }
  return d;
}

/* A closed tube outline: forward along the top edge, across the open end,
   back along the bottom edge. */
function tubePath(xs, hs, cy) {
  const top = xs.map((x, i) => [x, cy - hs[i]]);
  const bot = xs.map((x, i) => [x, cy + hs[i]]).reverse();
  return smoothPath(top) + smoothPath(bot, false) + ' Z';
}

const DEFAULT_BORE = [0.0145, 0.0146, 0.0151, 0.0158, 0.0168, 0.0182, 0.0198, 0.0218,
                      0.0242, 0.0268, 0.0298, 0.0331, 0.0367, 0.0407, 0.0450, 0.0500];
const DEFAULT_TRACT = [1.44, 2.92, 3.58, 2.56, 1.72, 2.08, 3.08, 3.80];

function InstrumentView({ bell, setBell, flare, setFlare, texture = 0.3, tractMix = 0.5, wallDamp = 0.3 }) {
  const lv = JuceBridge.useEventRef('levels', {
    out: [-90, -90], pressure: 0, lipOpen: 0, flow: 0,
    f0: 73.42, toot: 199, tootActive: false, playing: false,
    bore: DEFAULT_BORE, tract: DEFAULT_TRACT,
  });

  /* Params the loop reads without re-subscribing. */
  const P = useRef({});
  P.current = { texture, tractMix, wallDamp, bell, flare };

  const woodRef = useRef(null);
  const cavityRef = useRef(null);
  const innerEdgeRef = useRef(null);
  const grainRef = useRef(null);
  const segRef = useRef(null);
  const waveRef = useRef(null);
  const waveLineRef = useRef(null);
  const tractRef = useRef(null);
  const tractEdgeRef = useRef(null);
  const lipUpRef = useRef(null);
  const lipDnRef = useRef(null);
  const bellGlowRef = useRef(null);
  const noteRef = useRef(null);
  const hzRef = useRef(null);
  const tootRef = useRef(null);
  const tootChipRef = useRef(null);
  const droneChipRef = useRef(null);
  const svgRef = useRef(null);

  /* ---- drag zones: the bore is honest about which parameter owns which
     part of its profile, so dragging there edits that parameter ---- */
  const dragBell = useCallback((e) => beginVerticalDrag(e, P.current.bell, setBell), [setBell]);
  const dragFlare = useCallback((e) => beginVerticalDrag(e, P.current.flare, setFlare), [setFlare]);

  useEffect(() => {
    let raf = 0, last = performance.now();
    let phase = 0, level = 0, lipSm = 0, glowSm = 0;

    const xs = [];
    for (let i = 0; i < 16; i++) xs.push(BORE_X0 + (i / 15) * BORE_SPAN);

    // Fixed pseudo-random offsets so the wall grain does not crawl frame to frame.
    const grainSeed = [];
    for (let i = 0; i < 34; i++) {
      const s = Math.sin(i * 12.9898) * 43758.5453;
      grainSeed.push([(s - Math.floor(s)), ((s * 3.7) - Math.floor(s * 3.7))]);
    }

    const NW = 84;   // wave samples

    const tick = (now) => {
      const dt = Math.min(0.1, (now - last) / 1000); last = now;
      const L = lv.current || {};
      const pr = P.current;

      /* ---- bore geometry ---- */
      const bore = (L.bore && L.bore.length === 16) ? L.bore : DEFAULT_BORE;
      const hs = [], ho = [];
      for (let i = 0; i < 16; i++) {
        const h = Math.max(8, Math.min(MAX_HALF - WALL, bore[i] * M_PX));
        hs.push(h);
        ho.push(h + WALL);
      }
      const hAt = (u) => {
        const f = Math.max(0, Math.min(1, u)) * 15;
        const i = Math.min(14, Math.floor(f));
        return hs[i] + (hs[i + 1] - hs[i]) * (f - i);
      };

      if (woodRef.current) woodRef.current.setAttribute('d', tubePath(xs, ho, CY));
      const cav = tubePath(xs, hs, CY);
      if (cavityRef.current) cavityRef.current.setAttribute('d', cav);
      if (innerEdgeRef.current) innerEdgeRef.current.setAttribute('d', cav);

      /* Wall grain: short strokes lying inside the wall thickness. Density is
         fixed, contrast tracks the Texture parameter (rough vs polished bore). */
      let g = '';
      for (let i = 0; i < grainSeed.length; i++) {
        const u = grainSeed[i][0];
        const side = i % 2 ? 1 : -1;
        const frac = 0.18 + 0.64 * grainSeed[i][1];
        const len = 0.02 + 0.05 * grainSeed[i][1];
        const u2 = Math.min(1, u + len);
        const x1 = BORE_X0 + u * BORE_SPAN, x2 = BORE_X0 + u2 * BORE_SPAN;
        const y1 = CY + side * (hAt(u) + WALL * frac);
        const y2 = CY + side * (hAt(u2) + WALL * frac);
        g += `M ${x1.toFixed(1)} ${y1.toFixed(1)} L ${x2.toFixed(1)} ${y2.toFixed(1)} `;
      }
      if (grainRef.current) {
        grainRef.current.setAttribute('d', g);
        grainRef.current.setAttribute('opacity', (0.10 + 0.55 * pr.texture).toFixed(3));
      }

      /* Segment boundaries of the waveguide, drawn faintly across the bore —
         the drawing then shows the discretisation it is actually made of. */
      let sg = '';
      for (let i = 1; i < 15; i++)
        sg += `M ${xs[i].toFixed(1)} ${(CY - hs[i]).toFixed(1)} L ${xs[i].toFixed(1)} ${(CY + hs[i]).toFixed(1)} `;
      if (segRef.current) segRef.current.setAttribute('d', sg);

      /* ---- vocal tract inset ---- */
      const tr = (L.tract && L.tract.length === 8) ? L.tract : DEFAULT_TRACT;
      const txs = [], ths = [];
      for (let i = 0; i < 8; i++) {
        txs.push(TRACT_X0 + (i / 7) * (TRACT_X1 - TRACT_X0));
        ths.push(Math.sqrt(Math.max(0.05, tr[i]) / Math.PI) * CM_PX);
      }
      const td = tubePath(txs, ths, CY);
      if (tractRef.current) {
        tractRef.current.setAttribute('d', td);
        tractRef.current.setAttribute('opacity', (0.20 + 0.75 * pr.tractMix).toFixed(3));
      }
      if (tractEdgeRef.current) tractEdgeRef.current.setAttribute('d', td);

      /* ---- envelope + standing wave ---- */
      const target = Math.max(0, Math.min(1, Number(L.pressure) || 0));
      level += (target - level) * (1 - Math.exp(-dt / 0.09));

      const f0 = Number(L.f0) > 0 ? L.f0 : 73.42;
      /* The real drone sits far above the frame rate, so the animation is
         geared down by a fixed ratio: relative pitch still reads (a higher
         f0 pulses faster) without strobing. */
      phase += dt * 2 * Math.PI * (f0 / 22);
      if (phase > 1e6) phase -= 1e6;

      const toot = !!L.tootActive;
      const damp = 1 - 0.45 * pr.wallDamp;
      const top = [], bot = [];
      for (let i = 0; i <= NW; i++) {
        const u = i / NW;
        const x = BORE_X0 + u * BORE_SPAN;
        // Closed-open pipe: pressure antinode at the lips, node at the bell.
        let a = Math.cos(u * Math.PI / 2) * Math.cos(phase);
        if (toot) a += 0.62 * Math.cos(u * 3 * Math.PI / 2) * Math.cos(3 * phase);
        a += 0.22 * Math.cos(u * 7.5 - phase * 1.6);           // travelling component
        /* The air column is drawn as a lit body filling the bore, with the
           standing wave swelling and shrinking it. It deliberately keeps a
           floor everywhere: scaling by the pressure envelope alone would
           collapse the column to nothing at the bell — a real node, but it
           reads as an empty black tube rather than a sounding instrument. */
        const swell = 0.46 + 0.34 * a;
        const amp = hAt(u) * Math.max(0.12, swell) * damp * (0.34 + 0.66 * level);
        top.push([x, CY - amp]);
        bot.push([x, CY + amp]);
      }
      const wd = smoothPath(top) + smoothPath(bot.reverse(), false) + ' Z';
      if (waveRef.current) {
        waveRef.current.setAttribute('d', wd);
        waveRef.current.setAttribute('opacity', (0.34 + 0.56 * level).toFixed(3));
      }
      if (waveLineRef.current) {
        waveLineRef.current.setAttribute('d', smoothPath(top));
        waveLineRef.current.setAttribute('opacity', (0.25 + 0.65 * level).toFixed(3));
      }

      /* ---- lips ---- */
      const lipTarget = Math.max(0, Math.min(1, (Number(L.lipOpen) || 0) / 0.004));
      lipSm += (lipTarget - lipSm) * (1 - Math.exp(-dt / 0.05));
      const gap = 3 + lipSm * 26;
      const LIP_H = 46;
      if (lipUpRef.current) {
        lipUpRef.current.setAttribute('y', (CY - LIP_H).toFixed(1));
        lipUpRef.current.setAttribute('height', Math.max(4, LIP_H - gap / 2).toFixed(1));
      }
      if (lipDnRef.current) {
        lipDnRef.current.setAttribute('y', (CY + gap / 2).toFixed(1));
        lipDnRef.current.setAttribute('height', Math.max(4, LIP_H - gap / 2).toFixed(1));
      }

      /* ---- bell radiation glow ---- */
      const db = (L.out && L.out.length) ? L.out[0] : -90;
      const gt = Math.max(0, Math.min(1, (db + 54) / 54));
      glowSm += (gt - glowSm) * (1 - Math.exp(-dt / 0.12));
      if (bellGlowRef.current) {
        // Kept inside the viewBox: cx sits at BORE_X1 + 24, so rx must stay
        // under the remaining margin or the glow clips against the frame edge.
        bellGlowRef.current.setAttribute('rx', (22 + 56 * glowSm).toFixed(1));
        bellGlowRef.current.setAttribute('ry', (hs[15] + 24 + 34 * glowSm).toFixed(1));
        bellGlowRef.current.setAttribute('opacity', (0.10 + 0.5 * glowSm).toFixed(3));
      }

      /* ---- readouts ---- */
      if (noteRef.current) noteRef.current.textContent = hzToNote(f0);
      if (hzRef.current) hzRef.current.textContent = f0.toFixed(1) + ' Hz';
      const tf = Number(L.toot) > 0 ? L.toot : f0 * 2.7;
      if (tootRef.current) tootRef.current.textContent = hzToNote(tf) + ' · ' + tf.toFixed(0) + ' Hz';
      if (tootChipRef.current) tootChipRef.current.classList.toggle('live', toot);
      if (droneChipRef.current) droneChipRef.current.classList.toggle('live', !!L.playing);

      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, []);

  const zx = (f) => BORE_X0 + f * BORE_SPAN;

  return (
    <div className="ivwrap">
      <div className="ivchip drone" ref={droneChipRef}>
        <span className="dot" />
        <span className="lab">Drone</span>
        <span className="val" ref={noteRef}>D2</span>
        <span className="hz" ref={hzRef}>73.4 Hz</span>
      </div>
      <div className="ivchip toot" ref={tootChipRef}>
        <span className="lab">Toot</span>
        <span className="val" ref={tootRef}>—</span>
      </div>

      <svg ref={svgRef} className="ivsvg" viewBox={`0 0 ${VB_W} ${VB_H}`} preserveAspectRatio="xMidYMid meet">
        <defs>
          <linearGradient id="wood" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%"   stopColor="#3a2214" />
            <stop offset="22%"  stopColor="#8a5228" />
            <stop offset="46%"  stopColor="#a9682f" />
            <stop offset="70%"  stopColor="#6a3d1c" />
            <stop offset="100%" stopColor="#2b170c" />
          </linearGradient>
          <linearGradient id="cav" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%"   stopColor="#150c06" />
            <stop offset="50%"  stopColor="#0a0503" />
            <stop offset="100%" stopColor="#160d06" />
          </linearGradient>
          <linearGradient id="wave" x1="0" y1="0" x2="1" y2="0">
            <stop offset="0%"   stopColor="#ffd08a" />
            <stop offset="45%"  stopColor="#ff9d3c" />
            <stop offset="100%" stopColor="#e2562a" />
          </linearGradient>
          <radialGradient id="bellglow" cx="0.5" cy="0.5" r="0.5">
            <stop offset="0%"   stopColor="#ffb35a" stopOpacity="0.85" />
            <stop offset="100%" stopColor="#ff8a2a" stopOpacity="0" />
          </radialGradient>
          <linearGradient id="tractg" x1="0" y1="0" x2="1" y2="0">
            <stop offset="0%"   stopColor="#7a4a86" />
            <stop offset="100%" stopColor="#c4657a" />
          </linearGradient>
          <filter id="soft" x="-40%" y="-60%" width="180%" height="220%">
            <feGaussianBlur stdDeviation="7" />
          </filter>
        </defs>

        {/* centre axis */}
        <line className="iv-axis" x1={TRACT_X0} y1={CY} x2={BORE_X1 + 22} y2={CY} />

        {/* vocal tract inset */}
        <g className="iv-tract">
          <path ref={tractRef} d="" fill="url(#tractg)" />
          <path ref={tractEdgeRef} d="" className="iv-tract-edge" fill="none" />
          <line className="iv-glottis" x1={TRACT_X0 - 2} y1={CY - 26} x2={TRACT_X0 - 2} y2={CY + 26} />
          <text className="iv-cap" x={TRACT_X0 - 4} y={CY + 84}>VOCAL TRACT</text>
          <text className="iv-cap dim" x={TRACT_X0 - 4} y={CY - 72}>GLOTTIS</text>
        </g>

        {/* throat -> lips connector */}
        <path className="iv-neck" d={`M ${TRACT_X1} ${CY - 15} L ${LIP_X} ${CY - 22} L ${LIP_X} ${CY + 22} L ${TRACT_X1} ${CY + 15} Z`} />

        {/* bell radiation */}
        <ellipse ref={bellGlowRef} className="iv-bellglow" cx={BORE_X1 + 24} cy={CY} rx="30" ry="90"
                 fill="url(#bellglow)" filter="url(#soft)" />

        {/* instrument body */}
        <path ref={woodRef} d="" fill="url(#wood)" className="iv-wood" />
        <path ref={grainRef} d="" className="iv-grain" fill="none" />
        <path ref={cavityRef} d="" fill="url(#cav)" />
        <path ref={waveRef} d="" fill="url(#wave)" className="iv-wave" />
        <path ref={waveLineRef} d="" className="iv-waveline" fill="none" />
        <path ref={innerEdgeRef} d="" className="iv-inner" fill="none" />

        {/* lips */}
        <g className="iv-lips">
          <rect ref={lipUpRef} x={LIP_X} y={CY - 46} width={LIP_W} height="30" rx="9" />
          <rect ref={lipDnRef} x={LIP_X} y={CY + 16} width={LIP_W} height="30" rx="9" />
          <text className="iv-cap" x={LIP_X - 2} y={CY + 84}>LIPS</text>
        </g>

        {/* drag affordances — labels appear on hover */}
        <g className="iv-zone" onPointerDown={dragFlare}>
          <rect x={zx(FLARE_ZONE[0])} y={CY - 150} width={zx(FLARE_ZONE[1]) - zx(FLARE_ZONE[0])} height="300" />
          <text x={(zx(FLARE_ZONE[0]) + zx(FLARE_ZONE[1])) / 2} y={CY - 118}>FLARE — DRAG</text>
        </g>
        <g className="iv-zone" onPointerDown={dragBell}>
          <rect x={zx(BELL_ZONE[0])} y={CY - 170} width={BORE_X1 + 30 - zx(BELL_ZONE[0])} height="340" />
          <text x={(zx(BELL_ZONE[0]) + BORE_X1) / 2 + 10} y={CY - 140}>BELL — DRAG</text>
        </g>
      </svg>

      <div className="ivhint">drag the bore to shape it · double-click a knob to reset · hold shift for fine</div>
    </div>
  );
}

Object.assign(window, { InstrumentView, hzToNote });
