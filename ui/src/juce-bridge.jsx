/* ============================================================
   Qube · JUCE <-> React bridge
   ============================================================
   Hooks that read live state from APVTS via the JUCE 8
   WebSliderRelay / WebToggleButtonRelay / WebComboBoxRelay
   APIs. Two-way bound: the host can drive any control
   (automation, preset load) and the UI reflects it; user edits
   write back through the relay so the audio engine sees them.

   When there is no native backend (opened in a plain browser
   for design work / headless UI verification), a mock Juce
   object stands in so the whole UI runs with fake state.
   ============================================================ */

(function (global) {
  const { useState, useEffect, useCallback } = React;

  /* ---- Browser mock (design/dev mode, no plugin backend) ---- */
  if (!global.__JUCE__ || !global.__JUCE__.backend) {
    const mkEvent = () => {
      const ls = [];
      return { addListener: (f) => ls.push(f), removeListener: () => {}, fire: (...a) => ls.forEach((f) => f(...a)) };
    };
    const sliders = {}, toggles = {}, combos = {};
    const MOCK_DEFAULTS = {
      posX: 0.5, posY: 0.75, spread: 0.15, rotate: 0.5,
      motionRate: 0.5, motionRadius: 0.5, motionPhase: 0,
      distAmount: 0.5, airAbsorb: 0.5, doppler: 0,
      roomMix: 0.25, roomSize: 0.5, roomDamp: 0.5, masterGain: 0.667,
    };
    global.Juce = {
      getSliderState: (id) => sliders[id] || (sliders[id] = (() => {
        let v = MOCK_DEFAULTS[id] !== undefined ? MOCK_DEFAULTS[id] : 0.5;
        const ev = mkEvent();
        return {
          getNormalisedValue: () => v,
          setNormalisedValue: (nv) => { v = nv; ev.fire(); },
          valueChangedEvent: ev,
          sliderDragStarted: () => {}, sliderDragEnded: () => {},
        };
      })()),
      getToggleState: (id) => toggles[id] || (toggles[id] = (() => {
        let v = false;
        const ev = mkEvent();
        return { getValue: () => v, setValue: (nv) => { v = !!nv; ev.fire(); }, valueChangedEvent: ev };
      })()),
      getComboBoxState: (id) => combos[id] || (combos[id] = (() => {
        let idx = 0;
        const ev = mkEvent();
        return { getChoiceIndex: () => idx, setChoiceIndex: (ni) => { idx = ni; ev.fire(); }, valueChangedEvent: ev };
      })()),
      backend: (() => {
        const listeners = {};
        // Fake levels feed so meters + the room view are alive in a browser.
        let t = 0;
        setInterval(() => {
          t += 1 / 30;
          const cbs = listeners['levels'] || [];
          const pos = { x: 0.6 * Math.sin(t * 0.7), y: 0.6 * Math.cos(t * 0.7) };
          const lvl = (base) => base + 6 * Math.sin(t * 2.1) - 14;
          cbs.forEach((cb) => cb({
            spk: [lvl(-4), lvl(-7), lvl(-12), lvl(-9)],
            out: [lvl(-6), lvl(-8)],
            pos, mode: 1, outCh: 4, bpm: 120, playing: true,
          }));
          const pcs = listeners['presetInfo'] || [];
          pcs.forEach((cb) => cb({ name: 'Slow Orbit', dirty: false }));
        }, 33);
        return {
          addEventListener: (name, cb) => { (listeners[name] = listeners[name] || []).push(cb); },
          removeEventListener: (name, cb) => {
            const a = listeners[name] || [];
            const i = a.indexOf(cb);
            if (i >= 0) a.splice(i, 1);
          },
          emitEvent: () => {},
        };
      })(),
      getNativeFunction: (name) => {
        if (name === 'listFactoryPresets')
          return () => Promise.resolve(['Center Stage', 'Slow Orbit', 'Vertigo', 'Figure Eight',
                                       'Synced Pendulum', 'Front-Back Bounce', 'Haunted Hallway',
                                       'Fly-By', 'Wide & Close', 'Distant Storm']);
        if (name === 'listUserPresets') return () => Promise.resolve([]);
        return () => Promise.resolve();
      },
    };
    global.__QUBE_MOCK__ = true;
  }

  /* ---- Event emit to native (no-op under the mock) ---- */
  function emitNative(name, payload) {
    try {
      if (global.__JUCE__ && global.__JUCE__.backend)
        global.__JUCE__.backend.emitEvent(name, payload || {});
    } catch (_) {}
  }

  /* ---- Hooks ---- */
  function useJuceSlider(id) {
    const relay = global.Juce.getSliderState(id);
    const [v, setV] = useState(relay.getNormalisedValue());
    useEffect(() => {
      const onChanged = () => setV(relay.getNormalisedValue());
      relay.valueChangedEvent.addListener(onChanged);
      return undefined;
    }, [id]);
    const set = useCallback((nv) => {
      const c = Math.max(0, Math.min(1, nv));
      relay.setNormalisedValue(c);
      setV(c);
    }, [id]);
    return [v, set];
  }

  function useJuceToggle(id) {
    const relay = global.Juce.getToggleState(id);
    const [v, setV] = useState(!!relay.getValue());
    useEffect(() => {
      const onChanged = () => setV(!!relay.getValue());
      relay.valueChangedEvent.addListener(onChanged);
      return undefined;
    }, [id]);
    const set = useCallback((b) => { relay.setValue(!!b); setV(!!b); }, [id]);
    return [v, set];
  }

  function useJuceChoice(id, options) {
    const relay = global.Juce.getComboBoxState(id);
    const [idx, setIdx] = useState(relay.getChoiceIndex());
    useEffect(() => {
      const onChanged = () => setIdx(relay.getChoiceIndex());
      relay.valueChangedEvent.addListener(onChanged);
      return undefined;
    }, [id]);
    const setIndex = useCallback((ni) => {
      const c = Math.max(0, Math.min(options.length - 1, ni));
      relay.setChoiceIndex(c);
      setIdx(c);
    }, [id, options]);
    return [idx, setIndex];
  }

  // Native events emitted by the editor on its 30 Hz UI timer.
  function useJuceEvent(name, initial) {
    const [v, setV] = useState(initial);
    useEffect(() => {
      const cb = (e) => setV(e);
      global.Juce.backend.addEventListener(name, cb);
      return () => global.Juce.backend.removeEventListener(name, cb);
    }, [name]);
    return v;
  }

  /* ---- Display formatters (mirror the APVTS ranges in C++) ---- */
  const fmt = {
    // -1..+1 linear
    signed1: (n) => (n * 2 - 1).toFixed(2),
    pct: (n) => Math.round(n * 100) + '%',
    degrees180: (n) => Math.round(n * 360 - 180) + '°',
    degrees360: (n) => Math.round(n * 360) + '°',
    // motionRate: 0.02..8 Hz with setSkewForCentre(0.5)
    hz: (n) => {
      const min = 0.02, max = 8, skew = Math.log(0.5) / Math.log((0.5 - min) / (max - min));
      const v = min + (max - min) * Math.pow(n, 1 / skew);
      return (v < 1 ? v.toFixed(2) : v.toFixed(1)) + ' Hz';
    },
    // masterGain: -24..+12 dB linear
    db: (n) => (n * 36 - 24).toFixed(1) + ' dB',
  };

  global.JuceBridge = { useJuceSlider, useJuceToggle, useJuceChoice, useJuceEvent, emitNative, fmt };
})(window);
