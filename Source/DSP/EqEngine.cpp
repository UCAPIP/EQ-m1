#include "DSP/EqEngine.h"

namespace eqm1 {

void EqEngine::prepare(double sampleRate) {
    fs = sampleRate > 0.0 ? sampleRate : 48000.0;

    outGainSm.setSampleRate(fs, kSubBlock);
    autoGainSm.setSampleRate(fs, kSubBlock);
    soloMixSm.setSampleRate(fs, kSubBlock);
    bypassMixSm.setSampleRate(fs, kSubBlock);
    soloWidthSm.setSampleRate(fs, kSubBlock);

    for (auto& b : bands) {
        b.logFreqSm.setSampleRate(fs, kSubBlock);
        b.gainSm.setSampleRate(fs, kSubBlock);
        b.qSm.setSampleRate(fs, kSubBlock);
        b.enableSm.setSampleRate(fs, kSubBlock);
        b.moving = true;
        b.builtValid = false;
        b.wasSoloed = false;
    }

    prepared = true;

    updateSmoothers();
    snapAll();
    updateSmoothers();
    rebuildCoefficients();
    refreshAutoGain();
    autoGainSm.snap(autoGainTargetLin);
    reset();
}

void EqEngine::reset() {
    for (auto& b : bands) {
        clearStates(b.state);
        clearStates(b.soloState);
        clearStates(b.soloBpState);
    }
}

void EqEngine::clearStates(StateArray& state) noexcept {
    std::memset(static_cast<void*>(state), 0, sizeof(StateArray));
}

void EqEngine::setParameters(const EngineInput& input) {
    const bool changed = current != input;
    current = input;
    if (changed)
        refreshAutoGain();
}

void EqEngine::snapAll() {
    outGainSm.snapToTarget();
    autoGainSm.snapToTarget();
    soloMixSm.snapToTarget();
    bypassMixSm.snapToTarget();
    soloWidthSm.snapToTarget();
    for (auto& b : bands) {
        b.logFreqSm.snapToTarget();
        b.gainSm.snapToTarget();
        b.qSm.snapToTarget();
        b.enableSm.snapToTarget();
    }
}

void EqEngine::updateSmoothers() {
    anySolo = false;

    for (int i = 0; i < kMaxBands; ++i) {
        BandRuntime& b = bands[i];
        const BandParams& t = current.bands[i];

        b.logFreqSm.setTarget(std::log(clampFreq(t.frequency, fs)));
        b.gainSm.setTarget(t.gainDb);
        b.qSm.setTarget(t.q);
        b.enableSm.setTarget(t.enabled ? 1.0 : 0.0);

        b.moving = b.logFreqSm.advance();
        b.moving |= b.gainSm.advance();
        b.moving |= b.qSm.advance();
        b.moving |= b.enableSm.advance();
        b.enableMix = b.enableSm.get();

        b.smoothed = t;
        b.smoothed.frequency = std::exp(b.logFreqSm.get());
        b.smoothed.gainDb = b.gainSm.get();
        b.smoothed.q = b.qSm.get();

        if (t.solo && !b.wasSoloed) {
            clearStates(b.soloBpState);
            clearStates(b.soloState);
        }
        b.wasSoloed = t.solo;
        if (t.solo)
            anySolo = true;
    }

    const GlobalParams& g = current.globals;
    outGainSm.setTarget(dbToGain(g.outputGainDb));
    autoGainSm.setTarget(autoGainTargetLin);
    soloMixSm.setTarget(anySolo ? 1.0 : 0.0);
    bypassMixSm.setTarget(g.bypass ? 1.0 : 0.0);
    soloWidthSm.setTarget(g.soloWidthOct);

    outGainSm.advance();
    autoGainSm.advance();
    soloMixSm.advance();
    bypassMixSm.advance();
    soloWidthMoving = soloWidthSm.advance();
}

void EqEngine::rebuildCoefficients() {
    for (int i = 0; i < kMaxBands; ++i) {
        BandRuntime& b = bands[i];
        const BandParams& t = current.bands[i];

        const bool discreteChanged = !b.builtValid || t.type != b.built.type
                                  || t.slope != b.built.slope || t.channel != b.built.channel;

        if (!b.moving && !discreteChanged && !soloWidthMoving)
            continue;

        b.coeffs = buildBandCoefficients(b.smoothed, fs);
        b.soloCoeffs = b.coeffs;

        b.soloBp.clear();
        const double soloQ = qFromOctaveWidth(soloWidthSm.get());
        for (int s = 0; s < kSoloSections; ++s)
            b.soloBp.push(makeBandPass(b.smoothed.frequency, fs, soloQ));

        b.built = b.smoothed;
        b.builtValid = true;
        b.moving = false;
    }
}

void EqEngine::refreshAutoGain() {
    if (!current.globals.autoGain) {
        autoGainTargetLin = 1.0;
        return;
    }
    autoGainCurve.update(current.bands, current.numBands, fs);
    autoGainTargetLin = dbToGain(autoGainCurve.autoGainDb());
}

void EqEngine::filterChannelMode(ChannelMode mode, const CoefficientSet& set, StateArray& state,
                                 double* L, double* R, int n) {
    switch (mode) {
        case ChannelMode::Stereo:
            for (int i = 0; i < n; ++i) {
                L[i] = runSet(set, state[0], L[i]);
                R[i] = runSet(set, state[1], R[i]);
            }
            break;

        case ChannelMode::LeftOnly:
            for (int i = 0; i < n; ++i)
                L[i] = runSet(set, state[0], L[i]);
            break;

        case ChannelMode::RightOnly:
            for (int i = 0; i < n; ++i)
                R[i] = runSet(set, state[1], R[i]);
            break;

        case ChannelMode::MidOnly:
        case ChannelMode::SideOnly: {
            const bool isMid = (mode == ChannelMode::MidOnly);
            double mid[kSubBlock], side[kSubBlock];
            for (int i = 0; i < n; ++i) {
                mid[i] = 0.5 * (L[i] + R[i]);
                side[i] = 0.5 * (L[i] - R[i]);
            }
            double* buf = isMid ? mid : side;
            BiquadState* st = state[isMid ? 0 : 1];
            for (int i = 0; i < n; ++i)
                buf[i] = runSet(set, st, buf[i]);
            for (int i = 0; i < n; ++i) {
                L[i] = mid[i] + side[i];
                R[i] = mid[i] - side[i];
            }
            break;
        }

        default:
            break;
    }
}

void EqEngine::applyBand(BandRuntime& b, double* L, double* R, int n) {
    const double mix = b.enableMix;

    if (mix <= 1.0e-4) {
        // Keep the state fresh while the band is off so that enabling it later
        // cannot inject a stale-state transient.
        clearStates(b.state);
        return;
    }

    if (b.coeffs.numSections == 0)
        return;

    if (mix >= 0.9999) {
        filterChannelMode(b.smoothed.channel, b.coeffs, b.state, L, R, n);
        return;
    }

    double tmpL[kSubBlock], tmpR[kSubBlock];
    std::memcpy(tmpL, L, sizeof(double) * static_cast<size_t>(n));
    std::memcpy(tmpR, R, sizeof(double) * static_cast<size_t>(n));
    filterChannelMode(b.smoothed.channel, b.coeffs, b.state, L, R, n);
    for (int i = 0; i < n; ++i) {
        L[i] = tmpL[i] + mix * (L[i] - tmpL[i]);
        R[i] = tmpR[i] + mix * (R[i] - tmpR[i]);
    }
}

void EqEngine::applyMainChain(double* L, double* R, int n) {
    for (int i = 0; i < kMaxBands; ++i)
        applyBand(bands[i], L, R, n);
}

void EqEngine::accumulateSoloBand(int bandIndex, const double* dryL, const double* dryR,
                                  double* accL, double* accR, int n) {
    BandRuntime& b = bands[bandIndex];
    if (b.soloBp.numSections == 0 || b.soloCoeffs.numSections == 0)
        return;

    double tmpL[kSubBlock], tmpR[kSubBlock];
    std::memcpy(tmpL, dryL, sizeof(double) * static_cast<size_t>(n));
    std::memcpy(tmpR, dryR, sizeof(double) * static_cast<size_t>(n));

    filterChannelMode(b.smoothed.channel, b.soloBp, b.soloBpState, tmpL, tmpR, n);
    filterChannelMode(b.smoothed.channel, b.soloCoeffs, b.soloState, tmpL, tmpR, n);

    for (int i = 0; i < n; ++i) {
        accL[i] += tmpL[i];
        accR[i] += tmpR[i];
    }
}

}  // namespace eqm1
