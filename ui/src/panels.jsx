/* ============================================================
   QUBE · control panels — Motion, Space, Room, Output
   ============================================================ */

const { useJuceSlider, useJuceToggle, useJuceChoice, useJuceEvent, emitNative, fmt } = JuceBridge;

const MOTION_MODES = ['Manual', 'Orbit', 'Fig 8', 'Pend', 'Bounce', 'Random'];
const MOTION_DIVS  = ['8 bars', '4 bars', '2 bars', '1 bar', '1/2', '1/2T', '1/4', '1/4T', '1/8', '1/8T', '1/16'];
const OUTPUT_MODES = ['Auto', 'Quad', 'Binaural', 'UHJ', 'St. Mix'];
const RENDER_NAMES = ['QUAD 4.0', 'BINAURAL', 'STEREO UHJ', 'STEREO MIX'];

/* ---- MOTION ---- */
function MotionPanel() {
  const [mode, setMode]     = useJuceChoice('motionMode', MOTION_MODES);
  const [rate, setRate]     = useJuceSlider('motionRate');
  const [sync, setSync]     = useJuceToggle('motionSync');
  const [div, setDiv]       = useJuceChoice('motionDiv', MOTION_DIVS);
  const [radius, setRadius] = useJuceSlider('motionRadius');
  const [phase, setPhase]   = useJuceSlider('motionPhase');
  const [rev, setRev]       = useJuceToggle('motionReverse');

  const manual = mode === 0;

  return (
    <div className="panel">
      <PHead title="Motion" meta={manual ? 'STATIC' : (sync ? MOTION_DIVS[div] : fmt.hz(rate))} />
      <Seg index={mode} options={MOTION_MODES} onChange={setMode} violet />
      <div className="rowflex center" style={{ marginTop: 12, opacity: manual ? 0.35 : 1, pointerEvents: manual ? 'none' : 'auto' }}>
        {sync
          ? (
            <div className="knob violet" style={{ width: 66 }}>
              <div className="seg violet" style={{ flexDirection: 'column', width: 74, maxHeight: 64, overflowY: 'auto' }}>
                {MOTION_DIVS.map((d, i) => (
                  <button key={d} className={i === div ? 'on' : ''} style={{ flex: 'none', padding: '3px 4px' }}
                          onClick={() => setDiv(i)}>{d}</button>
                ))}
              </div>
              <div className="klabel" style={{ marginTop: 4 }}>Division</div>
            </div>
          )
          : <Knob value={rate} onChange={setRate} label="Rate" format={fmt.hz} violet defaultValue={0.5} />}
        <Knob value={radius} onChange={setRadius} label="Radius" format={fmt.pct} violet defaultValue={0.5} />
        <Knob value={phase} onChange={setPhase} label="Phase" format={fmt.degrees360} violet defaultValue={0} />
        <div style={{ display: 'flex', flexDirection: 'column', gap: 6, paddingTop: 8 }}>
          <Chip on={sync} onClick={() => setSync(!sync)}>Sync</Chip>
          <Chip on={rev} onClick={() => setRev(!rev)}>Rev</Chip>
        </div>
      </div>
    </div>
  );
}

/* ---- SPACE (position conditioning) ---- */
function SpacePanel() {
  const [spread, setSpread] = useJuceSlider('spread');
  const [rotate, setRotate] = useJuceSlider('rotate');
  const [dist, setDist]     = useJuceSlider('distAmount');
  const [air, setAir]       = useJuceSlider('airAbsorb');
  const [dop, setDop]       = useJuceSlider('doppler');

  return (
    <div className="panel">
      <PHead title="Space" />
      <div className="rowflex center">
        <Knob value={spread} onChange={setSpread} label="Spread" format={fmt.pct} defaultValue={0.15} />
        <Knob value={rotate} onChange={setRotate} label="Rotate" format={fmt.degrees180} bipolar defaultValue={0.5} />
        <Knob value={dist} onChange={setDist} label="Distance" format={fmt.pct} defaultValue={0.5} />
        <Knob value={air} onChange={setAir} label="Air" format={fmt.pct} defaultValue={0.5} />
        <Knob value={dop} onChange={setDop} label="Doppler" format={fmt.pct} defaultValue={0} />
      </div>
    </div>
  );
}

/* ---- ROOM (reverb) ---- */
function RoomPanel() {
  const [mix, setMix]   = useJuceSlider('roomMix');
  const [sz, setSz]     = useJuceSlider('roomSize');
  const [damp, setDamp] = useJuceSlider('roomDamp');

  return (
    <div className="panel">
      <PHead title="Room" />
      <div className="rowflex center">
        <Knob value={mix} onChange={setMix} label="Mix" format={fmt.pct} defaultValue={0.25} />
        <Knob value={sz} onChange={setSz} label="Size" format={fmt.pct} defaultValue={0.5} />
        <Knob value={damp} onChange={setDamp} label="Damp" format={fmt.pct} defaultValue={0.5} />
      </div>
    </div>
  );
}

/* ---- OUTPUT ---- */
function OutputPanel() {
  const [mode, setMode] = useJuceChoice('outputMode', OUTPUT_MODES);
  const [gain, setGain] = useJuceSlider('masterGain');
  const lv = useJuceEvent('levels', { spk: [-90, -90, -90, -90], out: [-90, -90], mode: 0, outCh: 2 });

  const resolved = RENDER_NAMES[lv.mode] || RENDER_NAMES[0];
  const stereoBus = (lv.outCh || 2) < 4;

  return (
    <div className="panel">
      <PHead title="Output" meta={resolved} />
      <Seg index={mode} options={OUTPUT_MODES} onChange={setMode} />
      {stereoBus && mode === 1 && (
        <div style={{ marginTop: 6, fontSize: 10, color: 'var(--meter-hi)', letterSpacing: '0.05em' }}>
          Stereo bus — quad falls back to binaural
        </div>
      )}
      <div className="rowflex" style={{ marginTop: 12, alignItems: 'center', gap: 18 }}>
        <Knob value={gain} onChange={setGain} label="Master" format={fmt.db} size="lg" defaultValue={0.667} />
        <div className="meters">
          <Meter db={lv.spk[0]} label="FL" />
          <Meter db={lv.spk[1]} label="FR" />
          <Meter db={lv.spk[2]} label="RL" />
          <Meter db={lv.spk[3]} label="RR" />
        </div>
        <div className="meters">
          {(lv.out || []).slice(0, stereoBus ? 2 : 4).map((d, i) => (
            <Meter key={i} db={d} label={['L', 'R', '3', '4'][i]} />
          ))}
        </div>
      </div>
    </div>
  );
}

/* ---- Preset browser modal ---- */
function PresetBrowser({ onClose, currentName }) {
  const [factory, setFactory] = useState([]);
  const [user, setUser] = useState([]);
  const [saveName, setSaveName] = useState('');

  useEffect(() => {
    let alive = true;
    try {
      Juce.getNativeFunction('listFactoryPresets')().then((a) => { if (alive && a) setFactory(Array.from(a)); });
      Juce.getNativeFunction('listUserPresets')().then((a) => { if (alive && a) setUser(Array.from(a)); });
    } catch (_) {}
    return () => { alive = false; };
  }, []);

  const load = (name) => { emitNative('preset_load', { name }); onClose(); };
  const save = () => {
    const n = saveName.trim();
    if (!n) return;
    emitNative('preset_save', { name: n });
    onClose();
  };

  return (
    <div className="modal-back" onClick={onClose}>
      <div className="modal" onClick={(e) => e.stopPropagation()}>
        <div className="mhead">
          <h3>Presets</h3>
          <button onClick={onClose}>✕</button>
        </div>
        <div className="mlist">
          <div className="msec">Factory</div>
          {factory.map((n) => (
            <div key={n} className={'mrow' + (n === currentName ? ' cur' : '')} onClick={() => load(n)}>
              <span>{n}</span><span className="cat">FACTORY</span>
            </div>
          ))}
          {user.length > 0 && <div className="msec">User</div>}
          {user.map((n) => (
            <div key={n} className={'mrow' + (n === currentName ? ' cur' : '')} onClick={() => load(n)}>
              <span>{n}</span><span className="cat">USER</span>
            </div>
          ))}
        </div>
        <div className="msave">
          <input placeholder="Save as…" value={saveName}
                 onChange={(e) => setSaveName(e.target.value)}
                 onKeyDown={(e) => e.key === 'Enter' && save()} />
          <button onClick={save}>SAVE</button>
        </div>
      </div>
    </div>
  );
}

Object.assign(window, { MotionPanel, SpacePanel, RoomPanel, OutputPanel, PresetBrowser,
                        MOTION_MODES, MOTION_DIVS, OUTPUT_MODES });
