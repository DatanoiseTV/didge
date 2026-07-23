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

#include "WebEditor.h"
#include "../PluginProcessor.h"
#include "../ParameterIDs.h"

#include <DidgeUIData.h>

#include <optional>
#include <vector>
#include <cstddef>
#include <iostream>

namespace didge
{
namespace
{
    // ----- BinaryData lookup ------------------------------------------------
    // juce_add_binary_data flattens each source file into NAME + NAMESize.
    // originalFilenames maps a URL path back to a resource.
    struct ResourceTable
    {
        struct Entry { juce::String filename; const char* data; int size; };
        std::vector<Entry> entries;

        ResourceTable()
        {
            for (int i = 0; i < DidgeUIData::namedResourceListSize; ++i)
            {
                const char* name = DidgeUIData::namedResourceList[i];
                int size = 0;
                const char* data = DidgeUIData::getNamedResource (name, size);
                if (data == nullptr) continue;
                const juce::String original (DidgeUIData::getNamedResourceOriginalFilename (name));
                entries.push_back ({ original, data, size });
            }
        }

        std::optional<juce::WebBrowserComponent::Resource> lookup (const juce::String& url) const
        {
            juce::String name = url.startsWithChar ('/') ? url.substring (1) : url;
            if (name.isEmpty()) name = "index.html";
            const auto slash = name.lastIndexOfChar ('/');
            if (slash >= 0) name = name.substring (slash + 1);
            const auto q = name.indexOfChar ('?');
            if (q >= 0) name = name.substring (0, q);

            for (const auto& e : entries)
            {
                if (e.filename == name)
                {
                    juce::WebBrowserComponent::Resource r;
                    r.data.assign (reinterpret_cast<const std::byte*> (e.data),
                                   reinterpret_cast<const std::byte*> (e.data) + (size_t) e.size);
                    r.mimeType = mimeForName (name);
                    return r;
                }
            }
            // 404: surfaced on stderr so a terminal-launched standalone shows
            // missing-resource bugs (the blank-window failure class) directly.
            std::cerr << "Didge WebEditor: resource not found — \"" << url
                      << "\" (looked up as \"" << name << "\")" << std::endl;
            return std::nullopt;
        }

        static juce::String mimeForName (const juce::String& name)
        {
            if (name.endsWith (".html"))  return "text/html";
            if (name.endsWith (".css"))   return "text/css";
            if (name.endsWith (".js") ||
                name.endsWith (".jsx") ||
                name.endsWith (".mjs"))   return "application/javascript";
            if (name.endsWith (".svg"))   return "image/svg+xml";
            if (name.endsWith (".png"))   return "image/png";
            if (name.endsWith (".json"))  return "application/json";
            return "application/octet-stream";
        }
    };

    static ResourceTable& resourceTable()
    {
        static ResourceTable t;
        return t;
    }

    // ----- Param wiring -----------------------------------------------------
    // Must match the IDs in src/ParameterIDs.h and the PARAM ids used by the
    // JS side (ui/src). A typo here becomes a dead control at runtime.
    constexpr const char* kFloatIds[] = {
        "pressure", "attack", "release", "vibRate", "vibDepth", "breathNoise",
        "decay", "sustain", "velAmount",
        "tension", "lipDamp", "embouchure", "bendRange",
        "tractMix", "vowelX", "vowelY", "growl", "growlPitch",
        "tune", "bell", "flare", "texture", "wallDamp",
        "spaceMix", "spaceSize", "outGain",
    };
    constexpr const char* kBoolIds[]   = { "decayOn" };
    constexpr const char* kChoiceIds[] = { "velTarget", "boreProfile", "material" };

    // Design canvas. The JS fit scaler letterbox-scales #plugin to the window;
    // the editor constrains resize to the same aspect so there are no borders.
    constexpr int kDesignW = 1280;
    constexpr int kDesignH = 830;
}

// ============================================================================
WebEditor::WebEditor (::DidgeAudioProcessor& proc)
    : juce::AudioProcessorEditor (&proc), didgeProcessor (proc)
{
    setResizable (true, true);
    setWantsKeyboardFocus (true);
    setResizeLimits (kDesignW / 2, kDesignH / 2, kDesignW * 7 / 4, kDesignH * 7 / 4);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kDesignW / (double) kDesignH);

   #if JUCE_LINUX
    // JUCE 8 WebKit is an X11 client; force GTK onto X11 so the XEmbed
    // reparent into the host window works under Wayland sessions.
    ::setenv ("GDK_BACKEND", "x11", 0);
    // Point JUCE's WebKit helper extraction at an exec-allowed, user-owned
    // dir ($XDG_RUNTIME_DIR, fallback ~/.cache/didge) — /tmp can be noexec.
    if (::getenv ("TMPDIR") == nullptr)
    {
        juce::File chosen;
        if (const auto* xdg = ::getenv ("XDG_RUNTIME_DIR"))
        {
            juce::File xdgDir { juce::String (xdg) };
            if (xdgDir.isDirectory())
            {
                chosen = xdgDir.getChildFile ("didge");
                chosen.createDirectory();
            }
        }
        if (chosen == juce::File())
        {
            chosen = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                        .getChildFile (".cache/didge");
            chosen.createDirectory();
        }
        ::setenv ("TMPDIR", chosen.getFullPathName().toRawUTF8(), 0);
    }
   #endif

    auto& apvts = didgeProcessor.getValueTreeState();

    juce::WebBrowserComponent::Options options;
    options = options
       #if JUCE_WINDOWS
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2{}
            .withUserDataFolder (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                     .getChildFile ("DidgeWebView2"))
            .withStatusBarDisabled()
            .withBuiltInErrorPageDisabled())
       #else
        .withBackend (juce::WebBrowserComponent::Options::Backend::defaultBackend)
       #endif
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withNativeIntegrationEnabled (true)
        .withResourceProvider (
            [] (const juce::String& url) { return resourceTable().lookup (url); },
            juce::URL (juce::WebBrowserComponent::getResourceProviderRoot()).getOrigin())
        .withUserScript ("window.DIDGE_VERSION_STR = 'v" DIDGE_VERSION " · " DIDGE_GIT_BRANCH "';")
        .withNativeFunction (juce::Identifier { "reloadUI" },
            [this] (const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                juce::MessageManager::callAsync ([this] { reloadWebView(); });
                complete (juce::var());
            })
        .withNativeFunction (juce::Identifier { "listFactoryPresets" },
            [this] (const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                juce::Array<juce::var> arr;
                for (const auto& n : didgeProcessor.getPresetManager().getFactoryNames())
                    arr.add (juce::var (n));
                complete (juce::var (arr));
            })
        .withNativeFunction (juce::Identifier { "listUserPresets" },
            [this] (const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                juce::Array<juce::var> arr;
                for (const auto& n : didgeProcessor.getPresetManager().getUserNames())
                    arr.add (juce::var (n));
                complete (juce::var (arr));
            })
        .withEventListener (juce::Identifier { "preset_prev" },
            [this] (juce::var) { didgeProcessor.getPresetManager().previous(); })
        .withEventListener (juce::Identifier { "preset_next" },
            [this] (juce::var) { didgeProcessor.getPresetManager().next(); })
        .withEventListener (juce::Identifier { "preset_load" },
            [this] (juce::var payload)
            {
                const auto name = payload.getProperty ("name", juce::String()).toString();
                if (name.isNotEmpty())
                    didgeProcessor.getPresetManager().loadByName (name);
            })
        .withEventListener (juce::Identifier { "preset_save" },
            [this] (juce::var payload)
            {
                const auto name = payload.getProperty ("name", juce::String()).toString().trim();
                if (name.isNotEmpty())
                    didgeProcessor.getPresetManager().saveUser (name);
            })
        .withEventListener (juce::Identifier { "preset_delete" },
            [this] (juce::var payload)
            {
                const auto name = payload.getProperty ("name", juce::String()).toString();
                if (name.isNotEmpty())
                    didgeProcessor.getPresetManager().deleteUser (name);
            });

    sliderBindings.reserve (juce::numElementsInArray (kFloatIds));
    toggleBindings.reserve (juce::numElementsInArray (kBoolIds));
    comboBindings .reserve (juce::numElementsInArray (kChoiceIds));

    for (auto id : kFloatIds)
    {
        SliderBinding b;
        b.relay = std::make_unique<juce::WebSliderRelay> (juce::String (id));
        options = options.withOptionsFrom (*b.relay);
        sliderBindings.push_back (std::move (b));
    }
    for (auto id : kBoolIds)
    {
        ToggleBinding b;
        b.relay = std::make_unique<juce::WebToggleButtonRelay> (juce::String (id));
        options = options.withOptionsFrom (*b.relay);
        toggleBindings.push_back (std::move (b));
    }
    for (auto id : kChoiceIds)
    {
        ComboBinding b;
        b.relay = std::make_unique<juce::WebComboBoxRelay> (juce::String (id));
        options = options.withOptionsFrom (*b.relay);
        comboBindings.push_back (std::move (b));
    }

    webView = std::make_unique<juce::WebBrowserComponent> (options);
    addAndMakeVisible (*webView);

    for (size_t i = 0; i < std::size (kFloatIds); ++i)
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (kFloatIds[i])))
            sliderBindings[i].attach = std::make_unique<juce::WebSliderParameterAttachment> (*p, *sliderBindings[i].relay, apvts.undoManager);
    for (size_t i = 0; i < std::size (kBoolIds); ++i)
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (kBoolIds[i])))
            toggleBindings[i].attach = std::make_unique<juce::WebToggleButtonParameterAttachment> (*p, *toggleBindings[i].relay, apvts.undoManager);
    for (size_t i = 0; i < std::size (kChoiceIds); ++i)
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (kChoiceIds[i])))
            comboBindings[i].attach = std::make_unique<juce::WebComboBoxParameterAttachment> (*p, *comboBindings[i].relay, apvts.undoManager);

    // Open at design size when the display can hold it, else the largest
    // same-aspect box that fits ~92% of the primary display.
    {
        int w = kDesignW, h = kDesignH;
        if (auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto area = disp->userBounds;
            const int maxW = juce::roundToInt ((double) area.getWidth()  * 0.92);
            const int maxH = juce::roundToInt ((double) area.getHeight() * 0.92);
            w = juce::jmin (kDesignW, maxW);
            h = juce::roundToInt ((double) w * kDesignH / kDesignW);
            if (h > maxH) { h = maxH; w = juce::roundToInt ((double) h * kDesignW / kDesignH); }
        }
        setSize (w, h);
    }

    webView->goToURL (juce::WebBrowserComponent::getResourceProviderRoot() + "index.html");
    startHealthWatchdog();
    startTimerHz (30);
}

WebEditor::~WebEditor() = default;

// ============================================================================
class WebEditor::WebViewFallback : public juce::Component
{
public:
    explicit WebViewFallback (std::function<void()> onReload)
        : reloadCallback (std::move (onReload))
    {
        reloadButton.setButtonText ("Reload UI");
        reloadButton.onClick = [this] { if (reloadCallback) reloadCallback(); };
        addAndMakeVisible (reloadButton);
    }

    void setDiagnostic (const juce::String& s) { diagText = s; repaint(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff07090f));
        auto bounds = getLocalBounds().reduced (40);
        auto card = bounds.withSizeKeepingCentre (520, 280);
        g.setColour (juce::Colour (0xff11151f));
        g.fillRoundedRectangle (card.toFloat(), 12.0f);
        g.setColour (juce::Colour (0xff2a3350));
        g.drawRoundedRectangle (card.toFloat(), 12.0f, 1.0f);

        auto inner = card.reduced (24);
        g.setColour (juce::Colour (0xff62e6ff));
        g.setFont (juce::Font (juce::FontOptions (16.0f).withStyle ("Bold")));
        g.drawText ("UI failed to load", inner.removeFromTop (24), juce::Justification::topLeft);

        inner.removeFromTop (10);
        g.setColour (juce::Colour (0xffb9c2d8));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawFittedText (
            "The WebView didn't reach its ready signal within 4 seconds. "
            "Common causes: the OS killed the WebView content process, a "
            "stale browser cache, or a JS error in a script tag. Reload "
            "retries the navigation; if that doesn't help, fully quit the "
            "host and reopen.",
            inner.removeFromTop (90), juce::Justification::topLeft, 5);

        inner.removeFromTop (8);
        if (! diagText.isEmpty())
        {
            g.setColour (juce::Colour (0xffff8888));
            g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                                     11.0f, juce::Font::plain)));
            g.drawFittedText (diagText, inner.removeFromTop (80),
                              juce::Justification::topLeft, 6);
        }
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (40)
                          .withSizeKeepingCentre (520, 280)
                          .reduced (24);
        reloadButton.setBounds (bounds.removeFromBottom (32).withWidth (120));
    }

private:
    juce::TextButton reloadButton;
    juce::String     diagText;
    std::function<void()> reloadCallback;
};

void WebEditor::startHealthWatchdog()
{
    webViewHealthy = false;
    healthTicksRemaining = 120;   // 4 s at 30 Hz
    healthPollEveryTicks = 4;
    if (fallback != nullptr && fallback->isVisible())
        fallback->setVisible (false);
}

void WebEditor::pollHealthOnce()
{
    if (webView == nullptr) return;
    webView->evaluateJavascript (
        "(function(){"
        "  if (window.__didgeReady === true) return 'ready';"
        "  if (window.__didgeMountError) return 'mount-error:' + window.__didgeMountError;"
        "  return 'pending';"
        "})()",
        [this] (juce::WebBrowserComponent::EvaluationResult result)
        {
            const juce::var* v = result.getResult();
            if (v == nullptr || ! v->isString())
                return;
            const auto s = v->toString();
            if (s == "ready")
            {
                webViewHealthy = true;
                healthTicksRemaining = 0;
                if (fallback != nullptr && fallback->isVisible())
                    fallback->setVisible (false);
            }
            else if (s.startsWith ("mount-error:"))
            {
                onWebViewWedged ("React mount threw:\n" + s.substring (12));
            }
        });
}

void WebEditor::onWebViewWedged (const juce::String& jsErrorIfAny)
{
    if (fallback == nullptr)
        fallback = std::make_unique<WebViewFallback> ([this] { reloadWebView(); });
    addAndMakeVisible (*fallback);
    fallback->setBounds (getLocalBounds());
    fallback->toFront (false);
    fallback->setDiagnostic (jsErrorIfAny);
    healthTicksRemaining = 0;
}

void WebEditor::reloadWebView()
{
    if (webView == nullptr) return;
    webView->goToURL (juce::WebBrowserComponent::getResourceProviderRoot() + "index.html");
    startHealthWatchdog();
}

void WebEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void WebEditor::resized()
{
    if (webView != nullptr)
        webView->setBounds (getLocalBounds());
    if (fallback != nullptr && fallback->isVisible())
        fallback->setBounds (getLocalBounds());
}

bool WebEditor::keyPressed (const juce::KeyPress& k)
{
    // Cmd/Ctrl+R reload works even when the WebView has gone dead.
    if (k.getModifiers().isCommandDown() && k.getTextCharacter() == 'r')
    {
        reloadWebView();
        return true;
    }
    return false;
}

void WebEditor::timerCallback()
{
    if (healthTicksRemaining > 0)
    {
        --healthTicksRemaining;
        if ((healthTicksRemaining % healthPollEveryTicks) == 0)
            pollHealthOnce();

        if (healthTicksRemaining == 0 && ! webViewHealthy)
            onWebViewWedged ({});
    }

    emitLevels();
    emitPresetInfo();
}

void WebEditor::emitLevels()
{
    if (webView == nullptr) return;

    auto toDb = [] (float lin) -> float
    {
        if (lin <= 1.0e-5f) return -90.0f;
        return 20.0f * std::log10 (lin);
    };

    auto& engine = didgeProcessor.getEngine();

    juce::Array<juce::var> outArr;
    for (int i = 0; i < 2; ++i)
        outArr.add (juce::var (toDb (engine.consumeOutPeak (i))));

    // Bore profile and vocal tract drive the cutaway drawing in the UI, so
    // what is on screen is the geometry the model is actually running.
    juce::Array<juce::var> bore;
    for (int i = 0; i < didge::Bore::kSegments; ++i)
        bore.add (juce::var (engine.vizBoreRadius (i)));

    juce::Array<juce::var> tract;
    for (int i = 0; i < didge::VocalTract::kSections; ++i)
        tract.add (juce::var (engine.vizTractArea (i)));

    // Standing wave and airflow along the bore, plus the lip motion itself.
    // The drawing is built from these rather than from an assumed mode shape,
    // so the nodes sit where the waveguide actually puts them.
    juce::Array<juce::var> press, flowSeg;
    for (int i = 0; i < didge::Bore::kSegments; ++i)
    {
        press.add (juce::var (engine.vizBorePressure (i)));
        flowSeg.add (juce::var (engine.vizBoreFlow (i)));
    }

    juce::Array<juce::var> lipWave;
    for (int i = 0; i < didge::DidgeEngine::kLipTraceLen; ++i)
        lipWave.add (juce::var (engine.vizLipTrace (i)));

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty ("out",        juce::var (outArr));
    root->setProperty ("pressure",   engine.vizPressure());
    root->setProperty ("lipOpen",    engine.vizLipOpen());
    root->setProperty ("flow",       engine.vizFlow());
    root->setProperty ("f0",         engine.vizF0());
    root->setProperty ("toot",       engine.vizToot());
    root->setProperty ("tootActive", engine.vizTootActive());
    root->setProperty ("playing",    engine.anyNoteHeld());
    root->setProperty ("bore",       juce::var (bore));
    root->setProperty ("tract",      juce::var (tract));
    root->setProperty ("press",      juce::var (press));
    root->setProperty ("flowSeg",    juce::var (flowSeg));
    root->setProperty ("lipWave",    juce::var (lipWave));
    root->setProperty ("meanFlow",   engine.vizMeanFlow());
    root->setProperty ("turb",       engine.vizTurbulence());

    webView->emitEventIfBrowserIsVisible (juce::Identifier { "levels" }, juce::var (root.get()));
}

void WebEditor::emitPresetInfo()
{
    if (webView == nullptr) return;

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty ("name",  didgeProcessor.getPresetManager().getCurrentName());
    obj->setProperty ("dirty", didgeProcessor.isCurrentPresetDirty());
    webView->emitEventIfBrowserIsVisible (juce::Identifier { "presetInfo" }, juce::var (obj.get()));
}

} // namespace didge
