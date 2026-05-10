#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

// ─────────────────────────────────────────────────────────────────────────────
// Channel index constants — keep in sync with the I/O layout table in the spec.
// ─────────────────────────────────────────────────────────────────────────────
namespace Ch {
    static constexpr int FL  = 0;   ///< Front Left
    static constexpr int FR  = 1;   ///< Front Right
    static constexpr int LFE = 2;   ///< Low Frequency Effects
    static constexpr int CC  = 3;   ///< Center
    static constexpr int SL  = 4;   ///< Surround Left
    static constexpr int SR  = 5;   ///< Surround Right
    static constexpr int CH6 = 6;   ///< Channel 6
    static constexpr int CH7 = 7;   ///< Channel 7
    static constexpr int Count = 8;
}

// ─────────────────────────────────────────────────────────────────────────────
// Parameter IDs — used for AudioProcessorValueTreeState and persistence.
// ─────────────────────────────────────────────────────────────────────────────
namespace ParamID {
    static constexpr auto Bypass       = "bypass";
    static constexpr auto UpmixSurr    = "upmix_surround";
    static constexpr auto UpmixGain    = "upmix_gain";
    static constexpr auto SilenceHold  = "silence_hold";
}

// ─────────────────────────────────────────────────────────────────────────────
// SignalDetector — per-block absolute-peak check, stateless.
//
// Matches the approach used by the reference StereoToSurroundUpmixer: a
// channel is considered "active" if any single sample in the current block
// exceeds the threshold in absolute value.  This gives one-block latency
// (typically 1–5 ms) with zero accumulator overhead.
//
// Threshold: −100 dBFS  →  linear = 10^(−100/20) ≈ 1×10⁻⁵
// This is far below any intentional audio signal yet safely above the
// floating-point noise floor of a silent digital channel.
// ─────────────────────────────────────────────────────────────────────────────
class SignalDetector
{
public:
    static constexpr float kThresholdLinear = 1.0e-5f;  ///< −100 dBFS

    /// Returns true if any sample in [data, data+numSamples) exceeds the
    /// threshold in absolute value.  No state is mutated; safe to call from
    /// the real-time audio thread without locking.
    static bool hasSignal (const float* data, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            if (std::abs (data[i]) > kThresholdLinear)
                return true;
        return false;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// AutoUpmixAudioProcessor
// ─────────────────────────────────────────────────────────────────────────────
class AutoUpmixAudioProcessor  : public juce::AudioProcessor
{
public:
    AutoUpmixAudioProcessor();
    ~AutoUpmixAudioProcessor() override;

    // ── AudioProcessor interface ─────────────────────────────────────────────
    void prepareToPlay  (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock   (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool                        hasEditor()    const override { return true; }

    const juce::String getName() const override { return "AutoUpmix"; }

    bool   acceptsMidi()  const override { return false; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()                        override { return 1; }
    int  getCurrentProgram()                     override { return 0; }
    void setCurrentProgram (int)                 override {}
    const juce::String getProgramName (int)      override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ── Bus layout ───────────────────────────────────────────────────────────
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    // ── Parameter tree (for UI binding and persistence) ──────────────────────
    juce::AudioProcessorValueTreeState apvts;

    // ── Metering output (read by the editor on the message thread) ───────────
    // Indexed [0..7] = inputs, [8..15] = outputs.
    std::array<std::atomic<float>, 16> meterLevels;

    /// True when upmix processing is currently active (set in processBlock).
    std::atomic<bool> upmixActive { false };

private:
    // ── Parameter helpers ────────────────────────────────────────────────────
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ── Peak-hold meter accumulators (per channel, input + output) ───────────
    // Updated in processBlock; read by the editor timer.
    std::array<float, Ch::Count> mInputPeaks  {};
    std::array<float, Ch::Count> mOutputPeaks {};

    /// Compute peak over a buffer channel and decay previous peak.
    float updatePeak (float currentPeak, const float* data, int numSamples) noexcept;

    // ── Silence hold: prevents upmix toggling on brief dips below threshold ─────
    double mSampleRate      = 44100.0; // stored in prepareToPlay
    int    mSilenceSamples  = 0;       // running count of consecutive silent samples

    // ── Upmix matrix coefficients (pre-computed, constant) ───────────────────
    // See spec / http://elias.altervista.org/html/3_speaker_matrix.html
    static constexpr float kScale_5_6      =  5.0f / 6.0f;         // ≈ -1.6 dB
    static constexpr float kScale_1_6      =  1.0f / 6.0f;         // ≈ -15.6 dB
    static constexpr float kScale_sqrt10_6 = 1.054092553f;          // sqrt(10)/6 ≈ -5.6 dB

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoUpmixAudioProcessor)
};
