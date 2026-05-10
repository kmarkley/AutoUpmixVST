// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kirk Markley

#include "PluginEditor.h"

// ─────────────────────────────────────────────────────────────────────────────
// LevelMeter
// ─────────────────────────────────────────────────────────────────────────────

LevelMeter::LevelMeter()
{
    setOpaque (false);
}

void LevelMeter::setLevel (float linearAmplitude)
{
    const float db = (linearAmplitude > 0.0f)
        ? juce::Decibels::gainToDecibels (linearAmplitude)
        : kMinDB;

    mCurrentDB = juce::jlimit (kMinDB, kMaxDB, db);

    if (mCurrentDB >= mPeakHoldDB)
    {
        // New peak: reset hold counter
        mPeakHoldDB   = mCurrentDB;
        mHoldCounter  = kHoldFrames;
    }
    else if (mHoldCounter > 0)
    {
        --mHoldCounter;
    }
    else
    {
        // Decay peak marker
        mPeakHoldDB = std::max (kMinDB, mPeakHoldDB - kDecayRate);
    }

    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();

    // Background
    g.setColour (juce::Colour (0xff1a1a1a));
    g.fillRect (bounds);

    // Map dBFS to normalised height [0,1]  (0 dB at top, kMinDB at bottom)
    auto dbToY = [&] (float dB) -> float {
        float norm = (dB - kMinDB) / (kMaxDB - kMinDB);
        return h * (1.0f - norm);   // top = 0 dB
    };

    // Fill bar — colour shifts green → yellow → red
    float norm = (mCurrentDB - kMinDB) / (kMaxDB - kMinDB);
    juce::Colour barColour = (norm > 0.9f)
        ? juce::Colours::red
        : (norm > 0.7f ? juce::Colours::yellow : juce::Colour (0xff00cc44));

    float barTop = dbToY (mCurrentDB);
    if (barTop < h)
    {
        g.setColour (barColour.withAlpha (0.85f));
        g.fillRect (2.0f, barTop, w - 4.0f, h - barTop);
    }

    // Peak-hold marker (thin horizontal line)
    if (mPeakHoldDB > kMinDB)
    {
        float markerY = dbToY (mPeakHoldDB);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawHorizontalLine (static_cast<int> (markerY), 1.0f, w - 1.0f);
    }

    // Border
    g.setColour (juce::Colour (0xff444444));
    g.drawRect (bounds, 1.0f);
}

void LevelMeter::resized() {}

// ─────────────────────────────────────────────────────────────────────────────
// AutoUpmixAudioProcessorEditor
// ─────────────────────────────────────────────────────────────────────────────

AutoUpmixAudioProcessorEditor::AutoUpmixAudioProcessorEditor (AutoUpmixAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    // ── Plugin window size ────────────────────────────────────────────────────
    // Sized for comfortable display on a Raspberry Pi with a small monitor.
    setSize (480, 480);

    // ── Bypass toggle ─────────────────────────────────────────────────────────
    addAndMakeVisible (bypassToggle);
    bypassAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        p.apvts, ParamID::Bypass, bypassToggle);
    bypassToggle.setTooltip ("Pass all channels through unchanged, disabling upmix processing.");

    // ── Surround toggle ───────────────────────────────────────────────────────
    addAndMakeVisible (surroundToggle);
    surroundAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        p.apvts, ParamID::UpmixSurr, surroundToggle);
    surroundToggle.setTooltip ("When on, derives Center and Surround channels from the stereo input. "
                               "When off, widens the stereo image across FL, FR, and CC only.");

    // ── Gain slider ───────────────────────────────────────────────────────────
    gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    gainSlider.setTextValueSuffix (" dB");
    gainSlider.setTooltip ("Output level of the upmixed channels relative to the input signal.");
    addAndMakeVisible (gainSlider);

    gainLabel.setText ("Upmix Gain", juce::dontSendNotification);
    gainLabel.attachToComponent (&gainSlider, true);
    addAndMakeVisible (gainLabel);

    gainAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        p.apvts, ParamID::UpmixGain, gainSlider);

    // ── Silence hold slider ───────────────────────────────────────────────────
    holdSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    holdSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    holdSlider.setTextValueSuffix (" s");
    holdSlider.setTooltip ("How long the upmix stays active after stereo signal drops below the threshold. "
                           "Prevents dropouts and level jumps during brief quiet passages.");
    addAndMakeVisible (holdSlider);

    holdLabel.setText ("Hold Time", juce::dontSendNotification);
    holdLabel.attachToComponent (&holdSlider, true);
    addAndMakeVisible (holdLabel);

    holdAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        p.apvts, ParamID::SilenceHold, holdSlider);

    // ── Signal threshold slider ───────────────────────────────────────────────
    thresholdSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    thresholdSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    thresholdSlider.setTextValueSuffix (" dBFS");
    thresholdSlider.setTooltip ("Level below which a channel is considered silent. "
                                "Raise if the upmix triggers on noise; lower if it misses quiet signals.");
    addAndMakeVisible (thresholdSlider);

    thresholdLabel.setText ("Threshold", juce::dontSendNotification);
    thresholdLabel.attachToComponent (&thresholdSlider, true);
    addAndMakeVisible (thresholdLabel);

    thresholdAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        p.apvts, ParamID::SilenceThreshold, thresholdSlider);

    // ── Upmix status label ────────────────────────────────────────────────────
    upmixStatusLabel.setJustificationType (juce::Justification::centred);
    upmixStatusLabel.setFont (juce::Font (14.0f, juce::Font::bold));
    addAndMakeVisible (upmixStatusLabel);

    // ── Level meters ──────────────────────────────────────────────────────────
    for (auto& m : meters)
        addAndMakeVisible (m);

    // ── Start UI refresh timer (≈30 fps) ──────────────────────────────────────
    startTimerHz (30);
}

AutoUpmixAudioProcessorEditor::~AutoUpmixAudioProcessorEditor()
{
    stopTimer();
}

// ── Layout ────────────────────────────────────────────────────────────────────

void AutoUpmixAudioProcessorEditor::resized()
{
    const int W = getWidth();
    const int H = getHeight();

    const int margin     = 10;
    const int ctrlH      = 24;    ///< height of each control row
    const int ctrlSpaceY = 30;    ///< vertical step between control rows
    const int labelW     = 90;    ///< left label column width
    const int meterW     = 20;    ///< width of each individual level meter
    const int meterGap   = 4;     ///< gap between input and output meter columns
    const int channelGap = 6;     ///< gap between channel pairs

    // ── Controls (top section) ────────────────────────────────────────────────
    int y = margin;

    bypassToggle.setBounds   (margin, y, W - 2 * margin, ctrlH);
    y += ctrlSpaceY;

    surroundToggle.setBounds (margin, y, W - 2 * margin, ctrlH);
    y += ctrlSpaceY;

    // Gain slider: label lives to the left (attached), slider fills rest
    gainSlider.setBounds     (margin + labelW, y, W - margin - labelW - margin, ctrlH);
    y += ctrlSpaceY;

    // Hold time slider
    holdSlider.setBounds      (margin + labelW, y, W - margin - labelW - margin, ctrlH);
    y += ctrlSpaceY;

    // Signal threshold slider
    thresholdSlider.setBounds (margin + labelW, y, W - margin - labelW - margin, ctrlH);
    y += ctrlSpaceY;

    upmixStatusLabel.setBounds (margin, y, W - 2 * margin, ctrlH);
    y += ctrlSpaceY + 4;

    // ── Level meters (bottom section) ─────────────────────────────────────────
    // Layout: for each channel, draw   [label] [in-meter] [gap] [out-meter]
    // Both columns are centred in the available width.
    //
    // Total width per channel slot:
    //   labelW + meterW + meterGap + meterW + channelGap  (repeated 8 times)

    const int slotW       = labelW + meterW + meterGap + meterW;
    const int totalSlots  = Ch::Count * (slotW + channelGap) - channelGap;
    const int meterAreaH  = H - y - margin;

    // Centre horizontally
    int xStart = (W - totalSlots) / 2;

    // Draw channel label ticks and meter positions
    juce::Font labelFont (10.0f);

    for (int ch = 0; ch < Ch::Count; ++ch)
    {
        int x = xStart + ch * (slotW + channelGap);

        // Input meter
        meters[ch].setBounds (x + labelW, y, meterW, meterAreaH);

        // Output meter (offset by meterW + meterGap)
        meters[Ch::Count + ch].setBounds (x + labelW + meterW + meterGap,
                                           y, meterW, meterAreaH);
    }
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void AutoUpmixAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff222222));

    // ── Channel labels under meters ───────────────────────────────────────────
    const int margin     = 10;
    const int labelW     = 90;
    const int meterW     = 20;
    const int meterGap   = 4;
    const int channelGap = 6;
    const int slotW      = labelW + meterW + meterGap + meterW;
    const int totalSlots = Ch::Count * (slotW + channelGap) - channelGap;

    const int W = getWidth();
    int xStart  = (W - totalSlots) / 2;

    g.setFont (juce::Font (10.0f));
    g.setColour (juce::Colours::lightgrey);

    // Column headers: "IN" and "OUT" above the first pair (drawn just above meters)
    int meterTop = meters[0].getY();
    for (int ch = 0; ch < Ch::Count; ++ch)
    {
        int x = xStart + ch * (slotW + channelGap);

        // Channel name label (left of input meter)
        g.drawFittedText (kChannelNames[ch],
                          x, meterTop - 14, labelW, 12,
                          juce::Justification::centredRight, 1);
    }

    // "IN" / "OUT" column headers above the meter area
    if (Ch::Count > 0)
    {
        int x0 = xStart + labelW;
        g.setColour (juce::Colour (0xff00cc44));
        g.drawText ("IN",  x0,                    meterTop - 14, meterW, 12,
                    juce::Justification::centred, true);
        g.setColour (juce::Colour (0xff4488ff));
        g.drawText ("OUT", x0 + meterW + meterGap, meterTop - 14, meterW, 12,
                    juce::Justification::centred, true);
    }

    // Plugin title
    g.setFont (juce::Font (16.0f, juce::Font::bold));
    g.setColour (juce::Colours::white);
    g.drawText ("AutoUpmix", 0, 0, getWidth(), 20, juce::Justification::centred, true);
}

// ── Timer callback (≈30 fps UI refresh) ──────────────────────────────────────

void AutoUpmixAudioProcessorEditor::timerCallback()
{
    // Update meter levels from the atomic array published by processBlock.
    for (int i = 0; i < 16; ++i)
    {
        float linear = processorRef.meterLevels[i].load (std::memory_order_relaxed);
        meters[i].setLevel (linear);
    }

    // Update the upmix-active status label.
    const bool active  = processorRef.upmixActive.load (std::memory_order_relaxed);
    const bool bypass  = processorRef.apvts.getRawParameterValue (ParamID::Bypass)->load() > 0.5f;

    juce::String statusText;
    juce::Colour statusColour;

    if (bypass)
    {
        statusText   = "BYPASSED";
        statusColour = juce::Colours::orange;
    }
    else if (active)
    {
        statusText   = "UPMIX ACTIVE";
        statusColour = juce::Colour (0xff00cc44);    ///< green
    }
    else
    {
        statusText   = "PASSTHROUGH";
        statusColour = juce::Colours::grey;
    }

    upmixStatusLabel.setText (statusText, juce::dontSendNotification);
    upmixStatusLabel.setColour (juce::Label::textColourId, statusColour);
}
