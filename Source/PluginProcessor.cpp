// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kirk Markley

#include "PluginProcessor.h"
#include "PluginEditor.h"

// ─────────────────────────────────────────────────────────────────────────────
// Parameter layout
// ─────────────────────────────────────────────────────────────────────────────

juce::AudioProcessorValueTreeState::ParameterLayout
AutoUpmixAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Bypass toggle
    layout.add (std::make_unique<juce::AudioParameterBool> (
        ParamID::Bypass, "Bypass", false));

    // Upmix-to-surround toggle
    layout.add (std::make_unique<juce::AudioParameterBool> (
        ParamID::UpmixSurr, "Upmix to Surround", false));

    // Upmix gain: -24 dB … 0 dB, default 0 dB
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParamID::UpmixGain,
        "Upmix Gain",
        juce::NormalisableRange<float> (-24.0f, 0.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("dB")));

    // Silence hold: 0 … 2 s, default 0.3 s
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParamID::SilenceHold,
        "Silence Hold",
        juce::NormalisableRange<float> (0.0f, 2.0f, 0.01f),
        0.3f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("s")));

    // Signal threshold: -120 … -40 dBFS, default -100 dBFS
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParamID::SilenceThreshold,
        "Signal Threshold",
        juce::NormalisableRange<float> (-120.0f, -40.0f, 1.0f),
        -100.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("dBFS")));

    return layout;
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

AutoUpmixAudioProcessor::AutoUpmixAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::discreteChannels (Ch::Count), true)
          .withOutput ("Output", juce::AudioChannelSet::discreteChannels (Ch::Count), true)),
      apvts (*this, nullptr, "AutoUpmixState", createParameterLayout())
{
    // Initialise meter atomics to silence (−inf)
    for (auto& m : meterLevels)
        m.store (0.0f);
}

AutoUpmixAudioProcessor::~AutoUpmixAudioProcessor() {}

// ─────────────────────────────────────────────────────────────────────────────
// Bus layout validation
// ─────────────────────────────────────────────────────────────────────────────

bool AutoUpmixAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Require exactly 8-in / 8-out discrete channels.
    auto& in  = layouts.getMainInputChannelSet();
    auto& out = layouts.getMainOutputChannelSet();

    return (in  == juce::AudioChannelSet::discreteChannels (Ch::Count) &&
            out == juce::AudioChannelSet::discreteChannels (Ch::Count));
}

// ─────────────────────────────────────────────────────────────────────────────
// Prepare / Release
// ─────────────────────────────────────────────────────────────────────────────

void AutoUpmixAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    mInputPeaks.fill  (0.0f);
    mOutputPeaks.fill (0.0f);
    mSampleRate        = sampleRate;
    mSilenceSamples    = 0;
    mAuxSilenceSamples = 0;
}

void AutoUpmixAudioProcessor::releaseResources() {}

// ─────────────────────────────────────────────────────────────────────────────
// Metering helper
// ─────────────────────────────────────────────────────────────────────────────

float AutoUpmixAudioProcessor::updatePeak (float currentPeak,
                                            const float* data,
                                            int numSamples) noexcept
{
    // Fast peak-hold: find max absolute value in block.
    float blockPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        blockPeak = std::max (blockPeak, std::abs (data[i]));

    // Hold: keep higher of old peak or new; decay slowly (≈ 20 dB/s is fine,
    // but here we simply pass the raw per-block max to the UI timer which
    // handles visual decay on the message thread).
    return std::max (currentPeak, blockPeak);
}

// ─────────────────────────────────────────────────────────────────────────────
// processBlock — the real-time audio engine
// ─────────────────────────────────────────────────────────────────────────────

void AutoUpmixAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;  ///< prevent denormal CPU spikes

    const int numSamples = buffer.getNumSamples();
    const int numChannels = std::min (buffer.getNumChannels(), Ch::Count);

    // ── Read parameters (atomic snapshot) ────────────────────────────────────
    const bool bypass     = apvts.getRawParameterValue (ParamID::Bypass)->load()    > 0.5f;
    const bool wantSurr   = apvts.getRawParameterValue (ParamID::UpmixSurr)->load() > 0.5f;
    const float gainDB    = apvts.getRawParameterValue (ParamID::UpmixGain)->load();
    const float gainLinear = juce::Decibels::decibelsToGain (gainDB);

    // ── Capture input peak levels for metering ────────────────────────────────
    for (int ch = 0; ch < numChannels; ++ch)
    {
        if (buffer.getNumChannels() > ch)
            mInputPeaks[ch] = updatePeak (0.0f, buffer.getReadPointer (ch), numSamples);
    }
    // Publish input peaks atomically for the editor
    for (int ch = 0; ch < Ch::Count; ++ch)
        meterLevels[ch].store (mInputPeaks[ch], std::memory_order_relaxed);

    // ── Bypass: passthrough, no processing ───────────────────────────────────
    if (bypass)
    {
        upmixActive.store (false, std::memory_order_relaxed);
        // No copy needed — input == output in-place buffer.
        for (int ch = 0; ch < Ch::Count; ++ch)
            mOutputPeaks[ch] = mInputPeaks[ch];

        for (int ch = 0; ch < Ch::Count; ++ch)
            meterLevels[Ch::Count + ch].store (mOutputPeaks[ch], std::memory_order_relaxed);
        return;
    }

    // ── Signal detection — per-block absolute peak, one-block latency ────────
    const float thresholdDB     = apvts.getRawParameterValue (ParamID::SilenceThreshold)->load();
    const float thresholdLinear = juce::Decibels::decibelsToGain (thresholdDB);

    std::array<bool, Ch::Count> hasSignal {};
    for (int ch = 0; ch < numChannels; ++ch)
        hasSignal[ch] = SignalDetector::hasSignal (buffer.getReadPointer (ch), numSamples, thresholdLinear);

    // ── Silence hold: keep upmix active briefly after stereo signal drops ────
    // Prevents FL/FR gain steps and CC dropouts on quiet passages.
    const float holdSecs    = apvts.getRawParameterValue (ParamID::SilenceHold)->load();
    const int   holdSamples = static_cast<int> (mSampleRate * holdSecs);

    // Stereo hold: upmix → passthrough
    bool stereoHasSignal = hasSignal[Ch::FL] || hasSignal[Ch::FR];
    if (stereoHasSignal)
        mSilenceSamples = 0;
    else
        mSilenceSamples += numSamples;

    bool heldActive  = (mSilenceSamples < holdSamples);
    bool shouldUpmix = stereoHasSignal || heldActive;

    // Aux hold: passthrough → upmix. Treat aux as "present" until hold expires
    // to avoid spurious upmix activations during brief quiet passages on ch 2-7.
    bool auxHasSignal = false;
    for (int ch = Ch::LFE; ch < Ch::Count; ++ch)
        auxHasSignal |= hasSignal[ch];

    if (auxHasSignal)
        mAuxSilenceSamples = 0;
    else
        mAuxSilenceSamples += numSamples;

    bool auxSignalPresent = auxHasSignal || (mAuxSilenceSamples < holdSamples);

    // ── Passthrough conditions ────────────────────────────────────────────────
    // 1. Aux signal present (or held) → don't upmix, leave buffer in-place.
    // 2. Stereo silent beyond hold window → same: leave buffer in-place.
    if (auxSignalPresent || !shouldUpmix)
    {
        upmixActive.store (false, std::memory_order_relaxed);
        for (int ch = 0; ch < Ch::Count; ++ch)
            mOutputPeaks[ch] = mInputPeaks[ch];

        for (int ch = 0; ch < Ch::Count; ++ch)
            meterLevels[Ch::Count + ch].store (mOutputPeaks[ch], std::memory_order_relaxed);
        return;
    }

    // ── Upmix is active ───────────────────────────────────────────────────────
    upmixActive.store (true, std::memory_order_relaxed);

    // Grab read pointers to the original input samples before we start
    // overwriting the buffer.  Copy FL and FR to temporary vectors so we
    // have stable source data throughout the matrix multiplication.
    std::vector<float> flIn (numSamples), frIn (numSamples);
    std::copy (buffer.getReadPointer (Ch::FL),
               buffer.getReadPointer (Ch::FL) + numSamples, flIn.begin());
    std::copy (buffer.getReadPointer (Ch::FR),
               buffer.getReadPointer (Ch::FR) + numSamples, frIn.begin());

    // Helper lambdas for common sample-wise operations --------------------
    // out[i] = a[i] * scale
    auto scale = [&] (float* out, const float* a, float s) {
        juce::FloatVectorOperations::copyWithMultiply (out, a, s, numSamples);
    };
    // out[i] = a[i] * sa  +  b[i] * sb
    auto mix2 = [&] (float* out, const float* a, float sa,
                                  const float* b, float sb) {
        juce::FloatVectorOperations::copyWithMultiply (out, a, sa, numSamples);
        juce::FloatVectorOperations::addWithMultiply  (out, b, sb, numSamples);
    };
    // Zero a channel
    auto zero = [&] (int ch) {
        buffer.clear (ch, 0, numSamples);
    };
    // Apply gain in-place
    auto applyGain = [&] (int ch) {
        if (gainLinear != 1.0f)
            buffer.applyGain (ch, 0, numSamples, gainLinear);
    };

    if (wantSurr)
    {
        // ── Surround upmix ────────────────────────────────────────────────────
        //
        //  FLo  = FLi × (5/6)
        //  FRo  = FRi × (5/6)
        //  LFEo = 0
        //  CCo  = FLi × sqrt(10)/6  +  FRi × sqrt(10)/6
        //  SLo  = FRi × (1/6)  [inverted → negative scale]
        //  SRo  = FLi × (1/6)  [inverted → negative scale]
        //  CH6o = 0
        //  CH7o = 0

        scale (buffer.getWritePointer (Ch::FL),  flIn.data(),  kScale_5_6);
        scale (buffer.getWritePointer (Ch::FR),  frIn.data(),  kScale_5_6);
        zero  (Ch::LFE);
        mix2  (buffer.getWritePointer (Ch::CC),
               flIn.data(), kScale_sqrt10_6,
               frIn.data(), kScale_sqrt10_6);
        scale (buffer.getWritePointer (Ch::SL), frIn.data(), -kScale_1_6);  ///< inverted
        scale (buffer.getWritePointer (Ch::SR), flIn.data(), -kScale_1_6);  ///< inverted
        zero  (Ch::CH6);
        zero  (Ch::CH7);
    }
    else
    {
        // ── Stereo-widening upmix (no dedicated surrounds) ────────────────────
        //
        //  FLo  = FLi × (5/6)  +  FRi × (-1/6)
        //  FRo  = FRi × (5/6)  +  FLi × (-1/6)
        //  LFEo = 0
        //  CCo  = FLi × sqrt(10)/6  +  FRi × sqrt(10)/6
        //  SLo  = 0
        //  SRo  = 0
        //  CH6o = 0
        //  CH7o = 0

        mix2  (buffer.getWritePointer (Ch::FL),
               flIn.data(),  kScale_5_6,
               frIn.data(), -kScale_1_6);
        mix2  (buffer.getWritePointer (Ch::FR),
               frIn.data(),  kScale_5_6,
               flIn.data(), -kScale_1_6);
        zero  (Ch::LFE);
        mix2  (buffer.getWritePointer (Ch::CC),
               flIn.data(), kScale_sqrt10_6,
               frIn.data(), kScale_sqrt10_6);
        zero  (Ch::SL);
        zero  (Ch::SR);
        zero  (Ch::CH6);
        zero  (Ch::CH7);
    }

    // Apply upmix gain to all output channels
    for (int ch = 0; ch < Ch::Count; ++ch)
        applyGain (ch);

    // Capture and publish output peaks for metering
    for (int ch = 0; ch < Ch::Count; ++ch)
        mOutputPeaks[ch] = updatePeak (0.0f, buffer.getReadPointer (ch), numSamples);

    for (int ch = 0; ch < Ch::Count; ++ch)
        meterLevels[Ch::Count + ch].store (mOutputPeaks[ch], std::memory_order_relaxed);
}

// ─────────────────────────────────────────────────────────────────────────────
// State persistence (XML via APVTS)
// ─────────────────────────────────────────────────────────────────────────────

void AutoUpmixAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void AutoUpmixAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// ─────────────────────────────────────────────────────────────────────────────
// Editor factory
// ─────────────────────────────────────────────────────────────────────────────

juce::AudioProcessorEditor* AutoUpmixAudioProcessor::createEditor()
{
    return new AutoUpmixAudioProcessorEditor (*this);
}

// ─────────────────────────────────────────────────────────────────────────────
// Plugin entry point (JUCE macro)
// ─────────────────────────────────────────────────────────────────────────────

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AutoUpmixAudioProcessor();
}
