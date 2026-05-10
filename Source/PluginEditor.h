// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kirk Markley

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// ─────────────────────────────────────────────────────────────────────────────
// LevelMeter — a simple vertical peak-hold bar displayed in dBFS.
//
// The owning component calls setLevel() with a linear amplitude value.
// Visual decay (peak-hold fallback) is handled internally.
// ─────────────────────────────────────────────────────────────────────────────
class LevelMeter : public juce::Component
{
public:
    LevelMeter();

    /// Called from the timer callback with a linear amplitude value [0, 1+].
    void setLevel (float linearAmplitude);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    // dBFS display range
    static constexpr float kMinDB = -60.0f;
    static constexpr float kMaxDB =   0.0f;

    // Peak-hold: how many frames to hold before starting decay
    static constexpr int kHoldFrames  = 30;   ///< ~1 s at 30 fps
    static constexpr float kDecayRate = 0.5f; ///< dB per timer tick

    float mCurrentDB  { kMinDB };
    float mPeakHoldDB { kMinDB };
    int   mHoldCounter { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
};

// ─────────────────────────────────────────────────────────────────────────────
// AutoUpmixAudioProcessorEditor
// ─────────────────────────────────────────────────────────────────────────────
class AutoUpmixAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    explicit AutoUpmixAudioProcessorEditor (AutoUpmixAudioProcessor&);
    ~AutoUpmixAudioProcessorEditor() override;

    void paint  (juce::Graphics&) override;
    void resized() override;

private:
    AutoUpmixAudioProcessor& processorRef;

    // ── Controls ─────────────────────────────────────────────────────────────
    juce::ToggleButton  bypassToggle    { "Bypass" };
    juce::ToggleButton  surroundToggle  { "Upmix to Surround" };
    juce::Slider        gainSlider;
    juce::Label         gainLabel;
    juce::Slider        holdSlider;
    juce::Label         holdLabel;
    juce::Slider        thresholdSlider;
    juce::Label         thresholdLabel;

    // ── Status indicator ─────────────────────────────────────────────────────
    juce::Label         upmixStatusLabel;

    // ── Parameter attachments (keep alive for the lifetime of the editor) ────
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>  bypassAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>  surroundAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>  gainAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>  holdAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>  thresholdAttach;

    // ── Level meters ─────────────────────────────────────────────────────────
    // [0..7] = inputs, [8..15] = outputs; matches processorRef.meterLevels indexing.
    std::array<LevelMeter, 16> meters;

    // Channel label strings
    static constexpr const char* kChannelNames[Ch::Count] = {
        "FL", "FR", "LFE", "CC", "SL", "SR", "CH6", "CH7"
    };

    // ── Tooltip support ───────────────────────────────────────────────────────
    juce::TooltipWindow tooltipWindow { this, 600 };  ///< 600 ms hover delay

    // ── Timer ─────────────────────────────────────────────────────────────────
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoUpmixAudioProcessorEditor)
};
