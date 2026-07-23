/*
  Didge — physically modeled didgeridoo
  Copyright (C) 2026 DatanoiseTV

  This program is free software: you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version, and distributed WITHOUT ANY WARRANTY. See <https://www.gnu.org/licenses/>.
  You must retain this notice and the attribution to DatanoiseTV in any
  redistributed or derivative version.
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>

class DidgeAudioProcessor;

namespace didge
{

// JUCE 8 WebView-based editor. Hosts a WebBrowserComponent that loads the
// vendored HTML/CSS/JSX bundle from BinaryData; every APVTS parameter is
// two-way bound to its matching DOM control via a WebSliderRelay plus the
// matching parameter attachment (every Didge parameter is continuous, so no
// toggle or combo relays are needed). Live metering, the model's own state —
// breath, lip opening, bore profile, vocal tract — and preset state are
// pushed to the JS side via emitEventIfBrowserIsVisible on a 30 Hz UI timer.
class WebEditor : public juce::AudioProcessorEditor,
                  private juce::Timer
{
public:
    explicit WebEditor (::DidgeAudioProcessor& proc);
    ~WebEditor() override;

    void paint   (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& k) override;

private:
    void timerCallback() override;
    void emitLevels();
    void emitPresetInfo();

    ::DidgeAudioProcessor& didgeProcessor;

    // Relay storage. CRITICAL: the relays are WebViewLifetimeListeners on the
    // browser; the browser's destructor walks its listener list, so the
    // relays MUST outlive the browser. Members destruct in reverse
    // declaration order — bindings are declared BEFORE `webView` so they are
    // destroyed after it.
    struct SliderBinding { std::unique_ptr<juce::WebSliderRelay>       relay; std::unique_ptr<juce::WebSliderParameterAttachment>       attach; };
    struct ToggleBinding { std::unique_ptr<juce::WebToggleButtonRelay> relay; std::unique_ptr<juce::WebToggleButtonParameterAttachment> attach; };
    struct ComboBinding  { std::unique_ptr<juce::WebComboBoxRelay>     relay; std::unique_ptr<juce::WebComboBoxParameterAttachment>     attach; };

    std::vector<SliderBinding> sliderBindings;
    std::vector<ToggleBinding> toggleBindings;
    std::vector<ComboBinding>  comboBindings;

    std::unique_ptr<juce::WebBrowserComponent> webView;

    // WebView health watchdog: if the page doesn't set window.__didgeReady
    // within the deadline, show a JUCE-side fallback panel with a Reload
    // button (a wedged WKWebView is otherwise a silent grey window).
    class WebViewFallback;
    std::unique_ptr<WebViewFallback> fallback;
    bool webViewHealthy       = false;
    int  healthTicksRemaining = 0;
    int  healthPollEveryTicks = 0;

    void startHealthWatchdog();
    void pollHealthOnce();
    void onWebViewWedged (const juce::String& jsErrorIfAny);
    void reloadWebView();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebEditor)
};

} // namespace didge
