/* ============================================================
   QUBE · RoomView — the draggable top-down room
   ============================================================
   A 2D canvas: speakers in the corners (level-reactive glow),
   listener in the centre, the source as a glowing puck with a
   motion trail, plus a live preview of the selected motion path.

   Rendering happens in a rAF loop reading refs — React state is
   only used for mount/unmount. The live source position arrives
   via the `levels` event (the ENGINE's post-motion position, so
   the puck traces exactly what plays); dragging writes the
   posX/posY parameters through the relays.

   Coordinates: world x -1..+1 left->right, y -1..+1 back->front.
   Screen: front is UP.
   ============================================================ */

function RoomView({ size = 700, posX, posY, setPosX, setPosY,
                    motionModeIdx, motionRadius, motionPhase, motionReverse,
                    spread, rotate }) {
  const canvasRef = useRef(null);
  const live = useRef({ pos: { x: 0, y: 0.5 }, spk: [-90, -90, -90, -90] });
  const trail = useRef([]);
  const dragRef = useRef(false);
  const hover = useRef(false);

  // Latest params in a ref so the rAF loop sees them without re-subscribing.
  const P = useRef({});
  P.current = { posX, posY, motionModeIdx, motionRadius, motionPhase, motionReverse, spread, rotate };

  /* ---- live data feed ---- */
  useEffect(() => {
    const cb = (e) => {
      if (e && e.pos) {
        live.current.pos = e.pos;
        live.current.spk = e.spk || live.current.spk;
        const now = performance.now();
        trail.current.push({ x: e.pos.x, y: e.pos.y, t: now });
        while (trail.current.length && now - trail.current[0].t > 2200) trail.current.shift();
      }
    };
    Juce.backend.addEventListener('levels', cb);
    return () => Juce.backend.removeEventListener('levels', cb);
  }, []);

  /* ---- drag to move the source ---- */
  const worldFromEvent = (e) => {
    const el = canvasRef.current;
    const r = el.getBoundingClientRect();
    // The canvas may be CSS-scaled by the fit transform; getBoundingClientRect
    // already reflects that, so normalise through its on-screen size.
    const nx = (e.clientX - r.left) / r.width;
    const ny = (e.clientY - r.top) / r.height;
    const pad = PAD / size;
    const wx = ((nx - pad) / (1 - 2 * pad)) * 2 - 1;
    const wy = -(((ny - pad) / (1 - 2 * pad)) * 2 - 1);
    return [Math.max(-1.1, Math.min(1.1, wx)), Math.max(-1.1, Math.min(1.1, wy))];
  };

  const onPointerDown = useCallback((e) => {
    e.preventDefault();
    dragRef.current = true;
    const [wx, wy] = worldFromEvent(e);
    setPosX((wx + 1) / 2);
    setPosY((wy + 1) / 2);
    const move = (ev) => {
      if (!dragRef.current) return;
      const [mx, my] = worldFromEvent(ev);
      setPosX((mx + 1) / 2);
      setPosY((my + 1) / 2);
    };
    const up = () => {
      dragRef.current = false;
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  }, [setPosX, setPosY]);

  const onDbl = useCallback(() => { setPosX(0.5); setPosY(0.75); }, [setPosX, setPosY]);

  const PAD = 46;   // px border around the room square

  /* ---- draw loop ---- */
  useEffect(() => {
    const el = canvasRef.current;
    const dpr = window.devicePixelRatio || 1;
    el.width = size * dpr;
    el.height = size * dpr;
    const ctx = el.getContext('2d');
    let raf = 0;
    let phase = 0;
    let lastT = performance.now();

    const w2s = (x, y) => {
      const half = (size - 2 * PAD) / 2;
      return [size / 2 + x * half, size / 2 - y * half];
    };

    const rot = (x, y, deg) => {
      const a = deg * Math.PI / 180;
      const c = Math.cos(a), s = Math.sin(a);
      return [c * x + s * y, -s * x + c * y];
    };

    const pathPoint = (mode, ph, r) => {
      const w = 2 * Math.PI * ph;
      switch (mode) {
        case 1: return [r * Math.sin(w), r * Math.cos(w)];
        case 2: return [r * Math.sin(w), 0.5 * r * Math.sin(2 * w)];
        case 3: return [r * Math.sin(w), 0];
        case 4: return [0, r * Math.cos(w)];
        default: return [0, 0];
      }
    };

    const draw = (now) => {
      const dt = (now - lastT) / 1000; lastT = now;
      phase = (phase + dt * 0.25) % 1;
      const p = P.current;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, size, size);

      const half = (size - 2 * PAD) / 2;
      const cx = size / 2, cy = size / 2;
      const x0 = cx - half, y0 = cy - half, side = half * 2;

      /* room floor */
      const floor = ctx.createRadialGradient(cx, cy, 40, cx, cy, half * 1.35);
      floor.addColorStop(0, 'rgba(56, 225, 255, 0.045)');
      floor.addColorStop(0.6, 'rgba(20, 30, 55, 0.25)');
      floor.addColorStop(1, 'rgba(6, 9, 16, 0.0)');
      ctx.fillStyle = floor;
      ctx.fillRect(0, 0, size, size);

      /* grid */
      ctx.strokeStyle = 'rgba(94, 116, 168, 0.10)';
      ctx.lineWidth = 1;
      const grid = 8;
      for (let i = 0; i <= grid; i++) {
        const t = x0 + (side * i) / grid;
        ctx.beginPath(); ctx.moveTo(t, y0); ctx.lineTo(t, y0 + side); ctx.stroke();
        ctx.beginPath(); ctx.moveTo(x0, t); ctx.lineTo(x0 + side, t); ctx.stroke();
      }
      /* centre cross-hair */
      ctx.strokeStyle = 'rgba(94, 116, 168, 0.20)';
      ctx.beginPath(); ctx.moveTo(cx, y0); ctx.lineTo(cx, y0 + side); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(x0, cy); ctx.lineTo(x0 + side, cy); ctx.stroke();

      /* distance rings */
      ctx.strokeStyle = 'rgba(94, 116, 168, 0.14)';
      ctx.setLineDash([2, 5]);
      for (const rr of [0.33, 0.66, 1.0]) {
        ctx.beginPath();
        ctx.arc(cx, cy, rr * half, 0, Math.PI * 2);
        ctx.stroke();
      }
      ctx.setLineDash([]);

      /* room border */
      ctx.strokeStyle = 'rgba(122, 148, 210, 0.35)';
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.roundRect(x0, y0, side, side, 14);
      ctx.stroke();

      /* speakers, with level glow. Order: FL FR RL RR. */
      const spkPos = [[-0.92, 0.92], [0.92, 0.92], [-0.92, -0.92], [0.92, -0.92]];
      const spkAng = [135, -135, 45, -45];   // icon rotation: aim at centre
      for (let i = 0; i < 4; i++) {
        const [sx, sy] = w2s(spkPos[i][0], spkPos[i][1]);
        const db = (live.current.spk && live.current.spk[i] !== undefined) ? live.current.spk[i] : -90;
        const lvl = Math.max(0, Math.min(1, (db + 54) / 54));

        if (lvl > 0.01) {
          const glow = ctx.createRadialGradient(sx, sy, 2, sx, sy, 26 + lvl * 46);
          glow.addColorStop(0, `rgba(56, 225, 255, ${0.35 * lvl})`);
          glow.addColorStop(1, 'rgba(56, 225, 255, 0)');
          ctx.fillStyle = glow;
          ctx.beginPath(); ctx.arc(sx, sy, 26 + lvl * 46, 0, Math.PI * 2); ctx.fill();
        }

        ctx.save();
        ctx.translate(sx, sy);
        ctx.rotate((spkAng[i] * Math.PI) / 180);
        ctx.fillStyle = `rgba(${Math.round(19 + lvl * 37)}, ${Math.round(26 + lvl * 160)}, ${Math.round(44 + lvl * 160)}, 1)`;
        ctx.strokeStyle = lvl > 0.02 ? `rgba(56, 225, 255, ${0.35 + 0.6 * lvl})` : 'rgba(122, 148, 210, 0.5)';
        ctx.lineWidth = 1.4;
        ctx.beginPath();
        ctx.roundRect(-11, -8, 22, 16, 3);
        ctx.fill(); ctx.stroke();
        /* driver */
        ctx.beginPath(); ctx.arc(0, 0, 4.4, 0, Math.PI * 2);
        ctx.strokeStyle = `rgba(200, 235, 255, ${0.35 + 0.6 * lvl})`;
        ctx.stroke();
        ctx.restore();

        /* label */
        ctx.fillStyle = 'rgba(138, 148, 173, 0.8)';
        ctx.font = '700 9px ui-sans-serif, system-ui';
        ctx.textAlign = 'center';
        const lbl = ['FL', 'FR', 'RL', 'RR'][i];
        ctx.fillText(lbl, sx, sy + (spkPos[i][1] > 0 ? -20 : 28));
      }

      /* listener (centre) */
      {
        const [lx, ly] = w2s(0, 0);
        ctx.fillStyle = 'rgba(20, 28, 48, 0.9)';
        ctx.strokeStyle = 'rgba(139, 123, 255, 0.6)';
        ctx.lineWidth = 1.4;
        ctx.beginPath(); ctx.arc(lx, ly, 11, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
        /* nose (faces front/up) */
        ctx.beginPath();
        ctx.moveTo(lx - 4, ly - 9); ctx.lineTo(lx, ly - 15); ctx.lineTo(lx + 4, ly - 9);
        ctx.closePath();
        ctx.fillStyle = 'rgba(139, 123, 255, 0.6)';
        ctx.fill();
        /* ears */
        ctx.fillStyle = 'rgba(139, 123, 255, 0.45)';
        ctx.beginPath(); ctx.arc(lx - 11, ly, 3, 0, Math.PI * 2); ctx.fill();
        ctx.beginPath(); ctx.arc(lx + 11, ly, 3, 0, Math.PI * 2); ctx.fill();
      }

      /* motion path preview (centered on the user's base position, rotated) */
      const baseX = p.posX * 2 - 1, baseY = p.posY * 2 - 1;
      const mode = p.motionModeIdx | 0;
      const rad = p.motionRadius;
      const rotDeg = p.rotate * 360 - 180;
      if (mode >= 1 && mode <= 4 && rad > 0.01) {
        ctx.strokeStyle = 'rgba(139, 123, 255, 0.38)';
        ctx.lineWidth = 1.4;
        ctx.setLineDash([4, 6]);
        ctx.beginPath();
        const N = 90;
        for (let i = 0; i <= N; i++) {
          const [ox, oy] = pathPoint(mode, i / N, rad);
          const [rxp, ryp] = rot(baseX + ox, baseY + oy, rotDeg);
          const [sx, sy] = w2s(Math.max(-1.18, Math.min(1.18, rxp)), Math.max(-1.18, Math.min(1.18, ryp)));
          i === 0 ? ctx.moveTo(sx, sy) : ctx.lineTo(sx, sy);
        }
        ctx.stroke();
        ctx.setLineDash([]);
      } else if (mode === 5 && rad > 0.01) {
        /* random: show the wander region */
        const [rxp, ryp] = rot(baseX, baseY, rotDeg);
        const [sx, sy] = w2s(rxp, ryp);
        ctx.strokeStyle = 'rgba(139, 123, 255, 0.3)';
        ctx.setLineDash([2, 6]);
        ctx.beginPath(); ctx.arc(sx, sy, rad * half, 0, Math.PI * 2); ctx.stroke();
        ctx.setLineDash([]);
      }

      /* trail of the actual engine position */
      const tr = trail.current;
      if (tr.length > 1) {
        ctx.lineCap = 'round';
        for (let i = 1; i < tr.length; i++) {
          const age = (now - tr[i].t) / 2200;
          const a = Math.max(0, 1 - age);
          const [ax, ay] = w2s(tr[i - 1].x, tr[i - 1].y);
          const [bx, by] = w2s(tr[i].x, tr[i].y);
          ctx.strokeStyle = `rgba(56, 225, 255, ${0.30 * a * a})`;
          ctx.lineWidth = 1 + 2.2 * a;
          ctx.beginPath(); ctx.moveTo(ax, ay); ctx.lineTo(bx, by); ctx.stroke();
        }
        ctx.lineCap = 'butt';
      }

      /* the source puck (live engine position) */
      const lp = live.current.pos;
      const [pxs, pys] = w2s(lp.x, lp.y);

      /* spread wedge: listener-centred sector around the source direction */
      if (p.spread > 0.03) {
        const theta = Math.atan2(pys - cy, pxs - cx);   // canvas angle
        const sg = p.spread * Math.PI / 2;
        const rr2 = Math.max(24, Math.hypot(pxs - cx, pys - cy) + 26);
        ctx.fillStyle = 'rgba(56, 225, 255, 0.07)';
        ctx.beginPath();
        ctx.moveTo(cx, cy);
        ctx.arc(cx, cy, rr2, theta - sg, theta + sg);
        ctx.closePath();
        ctx.fill();
      }

      /* line listener -> source */
      ctx.strokeStyle = 'rgba(56, 225, 255, 0.20)';
      ctx.lineWidth = 1;
      ctx.setLineDash([3, 5]);
      ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(pxs, pys); ctx.stroke();
      ctx.setLineDash([]);

      /* puck glow */
      const pg = ctx.createRadialGradient(pxs, pys, 2, pxs, pys, 34);
      pg.addColorStop(0, 'rgba(56, 225, 255, 0.75)');
      pg.addColorStop(0.35, 'rgba(56, 225, 255, 0.22)');
      pg.addColorStop(1, 'rgba(56, 225, 255, 0)');
      ctx.fillStyle = pg;
      ctx.beginPath(); ctx.arc(pxs, pys, 34, 0, Math.PI * 2); ctx.fill();

      /* puck body */
      const bg = ctx.createRadialGradient(pxs - 3, pys - 3, 1, pxs, pys, 11);
      bg.addColorStop(0, '#eafcff');
      bg.addColorStop(0.45, '#63e8ff');
      bg.addColorStop(1, '#0f7f96');
      ctx.fillStyle = bg;
      ctx.beginPath(); ctx.arc(pxs, pys, 10, 0, Math.PI * 2); ctx.fill();
      ctx.strokeStyle = 'rgba(234, 252, 255, 0.85)';
      ctx.lineWidth = 1.2;
      ctx.stroke();

      raf = requestAnimationFrame(draw);
    };
    raf = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(raf);
  }, [size]);

  return (
    <canvas
      ref={canvasRef}
      style={{ width: size, height: size, cursor: 'crosshair' }}
      onPointerDown={onPointerDown}
      onDoubleClick={onDbl}
      onPointerEnter={() => (hover.current = true)}
      onPointerLeave={() => (hover.current = false)}
    />
  );
}

window.RoomView = RoomView;
