/* ============================================================
   QUBE · interactive atoms — Knob, Chip, Seg, Meter
   ============================================================ */

/* ---- geometry helpers ---- */
function polar(cx, cy, r, deg) {
  const rad = (deg) * Math.PI / 180;
  return [cx + r * Math.sin(rad), cy - r * Math.cos(rad)];
}
function arcPath(cx, cy, r, a0, a1) {
  const [x0, y0] = polar(cx, cy, r, a0);
  const [x1, y1] = polar(cx, cy, r, a1);
  const large = (a1 - a0) > 180 ? 1 : 0;
  return `M ${x0.toFixed(2)} ${y0.toFixed(2)} A ${r} ${r} 0 ${large} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`;
}

const A0 = -135, A1 = 135, SWEEP = A1 - A0;

/* ---- Knob ----
   value is normalised 0..1; format converts normalised -> display string.
   bipolar draws the arc from the top-centre. */
function Knob({ value = 0.5, onChange, size = 'md', label, format, bipolar = false, violet = false, defaultValue = 0.5 }) {
  const [drag, setDrag] = useState(false);
  const stash = useRef({ y: 0, v: 0 });

  const D = size === 'lg' ? 86 : size === 'sm' ? 46 : 58;
  const sw = size === 'lg' ? 3.6 : size === 'sm' ? 2.6 : 3.0;
  const c = D / 2;
  const R = c - sw - 2;
  const ang = A0 + value * SWEEP;
  const bodyR = R - (size === 'lg' ? 9 : size === 'sm' ? 6 : 7.5);

  const [px, py] = polar(c, c, R, ang);
  const pointerInner = bodyR - (size === 'lg' ? 8 : 4);
  const [pix, piy] = polar(c, c, pointerInner, ang);

  const onDown = useCallback((e) => {
    e.preventDefault();
    const pt = e.touches ? e.touches[0] : e;
    stash.current = { y: pt.clientY, v: value };
    setDrag(true);
    const move = (ev) => {
      const p = ev.touches ? ev.touches[0] : ev;
      const fine = ev.shiftKey ? 0.25 : 1;
      // 240 px per full knob range; shift = quarter speed for fine trims.
      const dv = (stash.current.y - p.clientY) / 240 * fine;
      onChange && onChange(Math.max(0, Math.min(1, stash.current.v + dv)));
    };
    const up = () => {
      setDrag(false);
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  }, [value, onChange]);

  const onDbl = () => onChange && onChange(defaultValue);

  const ticks = [];
  const tickN = size === 'sm' ? 0 : 7;
  for (let i = 0; i < tickN; i++) {
    const a = A0 + (i / (tickN - 1)) * SWEEP;
    const [x1, y1] = polar(c, c, R + 3.5, a);
    const [x2, y2] = polar(c, c, R + 6.0, a);
    ticks.push(<line key={i} className="k-tick" x1={x1} y1={y1} x2={x2} y2={y2} strokeWidth="1" />);
  }

  const midAng = 0;
  const arc = bipolar
    ? (value >= 0.5 ? arcPath(c, c, R, midAng, ang) : arcPath(c, c, R, ang, midAng))
    : arcPath(c, c, R, A0, ang);

  const valTxt = format ? format(value) : Math.round(value * 100) + '%';

  return (
    <div className={'knob' + (violet ? ' violet' : '')}>
      <div className={'dial' + (drag ? ' dragging' : '')}
           onPointerDown={onDown} onDoubleClick={onDbl}
           style={{ width: D, height: D }}>
        <div className="kval">{valTxt}</div>
        <svg width={D} height={D} viewBox={`0 0 ${D} ${D}`} style={{ overflow: 'visible' }}>
          {ticks}
          <path className="k-track" d={arcPath(c, c, R, A0, A1)} fill="none" strokeWidth={sw} strokeLinecap="round" />
          {Math.abs(value - (bipolar ? 0.5 : 0)) > 0.001 &&
            <path className="k-arc" d={arc} fill="none" strokeWidth={sw} strokeLinecap="round" />}
          <circle className="k-body-out" cx={c} cy={c} r={bodyR + 2} />
          <circle className="k-body-in" cx={c} cy={c} r={bodyR} />
          <line className="k-point" x1={pix} y1={piy} x2={px - (px - c) * 0.06} y2={py - (py - c) * 0.06}
                strokeWidth={size === 'lg' ? 2.2 : 1.8} strokeLinecap="round" />
          <circle className="k-hub" cx={c} cy={c} r={size === 'lg' ? 2.6 : 2} />
        </svg>
      </div>
      {label && <div className="klabel">{label}</div>}
    </div>
  );
}

/* ---- Chip toggle (LED) ---- */
function Chip({ on, onClick, children, ...rest }) {
  return (
    <button className="chip" data-on={on ? '1' : '0'} onClick={onClick} {...rest}>
      <span className="led" />{children}
    </button>
  );
}

/* ---- Segmented control ---- */
function Seg({ index, options, onChange, violet = false }) {
  return (
    <div className={'seg' + (violet ? ' violet' : '')}>
      {options.map((o, i) => (
        <button key={o} className={i === index ? 'on' : ''} onClick={() => onChange && onChange(i)}>
          {o}
        </button>
      ))}
    </div>
  );
}

/* ---- Panel section header ---- */
function PHead({ title, meta }) {
  return (
    <div className="phead">
      <h2>{title}</h2>
      <span className="hrule" />
      {meta && <span className="hmeta">{meta}</span>}
    </div>
  );
}

/* ---- Vertical meter (dB in, -60..+6 scale) ----
   Animated by mutating the bar height from a rAF tick — going through React
   state would repaint at the 30 Hz event rate and fight the render loop. */
function Meter({ db = -90, label }) {
  const dbRef = useRef(db);
  dbRef.current = db;
  const barRef = useRef(null);
  const smoothRef = useRef(0);
  useEffect(() => {
    let raf = 0;
    let last = performance.now();
    const tick = (now) => {
      const dt = Math.min(0.1, (now - last) / 1000); last = now;
      const target = Math.max(0, Math.min(1, (dbRef.current + 60) / 66));
      const k = target > smoothRef.current ? 1 - Math.exp(-dt / 0.02) : 1 - Math.exp(-dt / 0.20);
      smoothRef.current += (target - smoothRef.current) * k;
      if (barRef.current) barRef.current.style.height = (smoothRef.current * 100) + '%';
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, []);
  return (
    <div className="meter">
      <div className="mtrack"><div className="mfill" ref={barRef} /></div>
      {label && <div className="mlabel">{label}</div>}
    </div>
  );
}

Object.assign(window, { Knob, Chip, Seg, PHead, Meter, polar, arcPath, A0, A1, SWEEP });
