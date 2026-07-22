/* ============================================================
   QUBE · app shell
   ============================================================ */

function Header({ onOpenBrowser }) {
  const preset = JuceBridge.useJuceEvent('presetInfo', { name: '—', dirty: false });

  return (
    <div className="hdr">
      <div className="mark"><div className="sq" /><div className="dot" /></div>
      <div className="wordmark">
        <div className="name">QUBE</div>
        <div className="tag">Quadraphonic Panner</div>
      </div>
      <div className="spacer" />
      <div className="presetbar">
        <button className="pbtn" onClick={() => JuceBridge.emitNative('preset_prev')} title="Previous preset">‹</button>
        <div className="pname" onClick={onOpenBrowser} title="Browse presets">
          {preset.name}{preset.dirty && <span className="dirty">*</span>}
        </div>
        <button className="pbtn" onClick={() => JuceBridge.emitNative('preset_next')} title="Next preset">›</button>
        <button className="psave" onClick={onOpenBrowser}>SAVE</button>
      </div>
      <div className="spacer" />
      <div className="version">{window.QUBE_VERSION_STR || 'dev'}</div>
    </div>
  );
}

function App() {
  const { useJuceSlider, useJuceChoice, useJuceToggle, useJuceEvent } = JuceBridge;

  // Room view needs the base position + path params for the preview overlay.
  const [posX, setPosX]   = useJuceSlider('posX');
  const [posY, setPosY]   = useJuceSlider('posY');
  const [spread]          = useJuceSlider('spread');
  const [rotate]          = useJuceSlider('rotate');
  const [motionMode]      = useJuceChoice('motionMode', MOTION_MODES);
  const [motionRadius]    = useJuceSlider('motionRadius');
  const [motionPhase]     = useJuceSlider('motionPhase');
  const [motionReverse]   = useJuceToggle('motionReverse');

  const lv = useJuceEvent('levels', { pos: { x: 0, y: 0.5 }, mode: 0, outCh: 2 });
  const preset = useJuceEvent('presetInfo', { name: '—', dirty: false });

  const [browser, setBrowser] = React.useState(false);

  const modeName = ['QUAD 4.0', 'BINAURAL', 'STEREO UHJ', 'STEREO MIX'][lv.mode] || 'QUAD 4.0';
  const px = (lv.pos && lv.pos.x) || 0;
  const py = (lv.pos && lv.pos.y) || 0;
  const az = Math.atan2(px, py) * 180 / Math.PI;
  const dist = Math.hypot(px, py);

  return (
    <div id="stage">
      <div id="plugin">
        <Header onOpenBrowser={() => setBrowser(true)} />
        <div className="main">
          <div className="roomcard">
            <div className="poschip">
              {`AZ ${az >= 0 ? '+' : ''}${az.toFixed(0)}°  ·  DIST ${dist.toFixed(2)}`}
            </div>
            <div className="modechip">{modeName}</div>
            <RoomView
              size={700}
              posX={posX} posY={posY} setPosX={setPosX} setPosY={setPosY}
              motionModeIdx={motionMode} motionRadius={motionRadius}
              motionPhase={motionPhase} motionReverse={motionReverse}
              spread={spread} rotate={rotate}
            />
            <div className="hint">drag to place · double-click to reset · shift for fine</div>
          </div>
          <div className="col">
            <MotionPanel />
            <SpacePanel />
            <RoomPanel />
            <OutputPanel />
          </div>
        </div>
        {browser && <PresetBrowser onClose={() => setBrowser(false)} currentName={preset.name} />}
      </div>
    </div>
  );
}

/* ---- mount ---- */
try {
  const root = ReactDOM.createRoot(document.getElementById('root'));
  root.render(<App />);
  window.__qubeReady = true;
} catch (err) {
  window.__qubeMountError = String((err && err.stack) || err);
  throw err;
}
