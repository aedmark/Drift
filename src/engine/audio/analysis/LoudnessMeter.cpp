#include "engine/audio/analysis/LoudnessMeter.h"

#include <QMutex>
#include <QMutexLocker>

#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace drift {
namespace {

// Direct-form I biquad. Coefficients are computed for the actual sample rate so a
// waveform-rate mix (8 kHz) and a loudness pass (48 kHz) share this file.
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    double process(double x)
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

// BS.1770 K-weighting: the standard's high shelf and RLB high-pass, re-derived for the actual
// sample rate. At 48 kHz these reproduce the coefficients tabulated in the standard; the textbook
// RBJ shelf and a Q 0.707 high-pass read about 0.7 dB hot on typical material.
Biquad makeKShelf(double sampleRate)
{
    constexpr double f0 = 1681.974450955533;
    constexpr double gainDb = 3.999843853973347;
    constexpr double q = 0.7071752369554196;
    const double k = std::tan(M_PI * f0 / sampleRate);
    const double vh = std::pow(10.0, gainDb / 20.0);
    const double vb = std::pow(vh, 0.4996667741545416);
    const double a0 = 1.0 + k / q + k * k;
    Biquad f;
    f.b0 = (vh + vb * k / q + k * k) / a0;
    f.b1 = 2.0 * (k * k - vh) / a0;
    f.b2 = (vh - vb * k / q + k * k) / a0;
    f.a1 = 2.0 * (k * k - 1.0) / a0;
    f.a2 = (1.0 - k / q + k * k) / a0;
    return f;
}

Biquad makeKHighPass(double sampleRate)
{
    constexpr double f0 = 38.13547087602444;
    constexpr double q = 0.5003270373238773;
    const double k = std::tan(M_PI * f0 / sampleRate);
    const double a0 = 1.0 + k / q + k * k;
    Biquad f;
    f.b0 = 1.0;
    f.b1 = -2.0;
    f.b2 = 1.0;
    f.a1 = 2.0 * (k * k - 1.0) / a0;
    f.a2 = (1.0 - k / q + k * k) / a0;
    return f;
}

constexpr int kChunkFrames = 1 << 16;

// BS.1770 measures true peak on an oversampled signal: two consecutive samples can both sit under
// full scale while the waveform between them does not. Linear interpolation cannot show that —
// a straight line between two points never leaves the range they already span — so the peak has to
// come from a band-limited reconstruction. 4x is the minimum the standard accepts.
constexpr int kOversample = 4;
constexpr int kTapsPerPhase = 12;
constexpr int kKernelLength = kOversample * kTapsPerPhase;

const std::array<double, kKernelLength> &truePeakKernel()
{
    static const std::array<double, kKernelLength> kernel = [] {
        std::array<double, kKernelLength> h{};
        const double center = (kKernelLength - 1) / 2.0;
        for (int i = 0; i < kKernelLength; ++i) {
            const double x = (i - center) / double(kOversample);
            // Windowed sinc: a lowpass at the original Nyquist, which is what reconstructs the
            // waveform between samples. The Hann window keeps the ripple down at this length.
            const double sinc = qFuzzyIsNull(x) ? 1.0 : std::sin(M_PI * x) / (M_PI * x);
            const double window = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (kKernelLength - 1));
            h[i] = sinc * window;
        }
        // Normalise each phase to unity gain, so a steady signal reconstructs at its own level and
        // the estimate can only ever be raised by real inter-sample content.
        for (int phase = 0; phase < kOversample; ++phase) {
            double sum = 0.0;
            for (int tap = 0; tap < kTapsPerPhase; ++tap)
                sum += h[phase + tap * kOversample];
            if (!qFuzzyIsNull(sum)) {
                for (int tap = 0; tap < kTapsPerPhase; ++tap)
                    h[phase + tap * kOversample] /= sum;
            }
        }
        return h;
    }();
    return kernel;
}

// Tracks one channel's recent samples and reports the largest reconstructed magnitude.
class TruePeakFollower
{
public:
    double push(double sample)
    {
        m_history[m_cursor] = sample;
        m_cursor = (m_cursor + 1) % kTapsPerPhase;

        const std::array<double, kKernelLength> &h = truePeakKernel();
        double peak = qAbs(sample);
        for (int phase = 0; phase < kOversample; ++phase) {
            double acc = 0.0;
            for (int tap = 0; tap < kTapsPerPhase; ++tap) {
                // Newest sample first, walking back through the ring.
                const int idx = (m_cursor - 1 - tap + kTapsPerPhase * 2) % kTapsPerPhase;
                acc += m_history[idx] * h[phase + tap * kOversample];
            }
            peak = qMax(peak, qAbs(acc));
        }
        return peak;
    }

private:
    std::array<double, kTapsPerPhase> m_history{};
    int m_cursor = 0;
};

} // namespace

LoudnessResult measureLoudness(qint64 totalFrames, int sampleRate,
                               const MediaWaveform::FillChunk &fill)
{
    LoudnessResult result;
    if (totalFrames <= 0 || sampleRate <= 0 || !fill)
        return result;

    result.durationSeconds = double(totalFrames) / double(sampleRate);

    Biquad hpL = makeKHighPass(sampleRate);
    Biquad hpR = makeKHighPass(sampleRate);
    Biquad shL = makeKShelf(sampleRate);
    Biquad shR = makeKShelf(sampleRate);

    // BS.1770 sums the per-channel mean squares with weight 1.0 for L and R; averaging them
    // instead is a flat -3.01 dB error on every reading, so the blocks below add, not mean.
    const int hop = qMax(1, sampleRate / 10);          // 100 ms
    const int block = qMax(hop, sampleRate * 4 / 10);  // 400 ms
    QVector<double> blockEnergy;

    double truePeak = 1e-12;
    double blockAcc[2] = {0.0, 0.0};
    int blockCount = 0;
    int sinceHop = 0;
    TruePeakFollower peakL;
    TruePeakFollower peakR;

    QVector<float> chunk(static_cast<qsizetype>(kChunkFrames) * 2);
    for (qint64 done = 0; done < totalFrames;) {
        const int want = static_cast<int>(qMin<qint64>(kChunkFrames, totalFrames - done));
        const int got = fill(chunk.data(), done, want);
        if (got <= 0)
            break;
        for (int i = 0; i < got; ++i) {
            const double l = chunk[i * 2];
            const double r = chunk[i * 2 + 1];
            truePeak = qMax(truePeak, peakL.push(l));
            truePeak = qMax(truePeak, peakR.push(r));

            const double kl = shL.process(hpL.process(l));
            const double kr = shR.process(hpR.process(r));
            blockAcc[0] += kl * kl;
            blockAcc[1] += kr * kr;
            ++blockCount;
            ++sinceHop;
            if (sinceHop >= hop && blockCount >= block) {
                const double mean = (blockAcc[0] + blockAcc[1]) / double(blockCount);
                blockEnergy.append(mean);
                // Overlap 75%: drop the oldest 100 ms of energy.
                const double dropFrac = double(hop) / double(blockCount);
                blockAcc[0] *= (1.0 - dropFrac);
                blockAcc[1] *= (1.0 - dropFrac);
                blockCount -= hop;
                sinceHop = 0;
            }
        }
        done += got;
    }
    if (blockCount > 0) {
        const double mean = (blockAcc[0] + blockAcc[1]) / double(blockCount);
        blockEnergy.append(mean);
    }

    auto lufsOf = [](double meanSquare) {
        if (meanSquare <= 1e-12)
            return -70.0;
        return -0.691 + 10.0 * std::log10(meanSquare);
    };

    // Absolute gate at -70 LUFS, then relative gate at gated loudness − 10 LU.
    double sum = 0.0;
    int n = 0;
    for (double e : blockEnergy) {
        if (lufsOf(e) > -70.0) {
            sum += e;
            ++n;
        }
    }
    const double absGated = n > 0 ? sum / double(n) : 0.0;
    const double relThresh = lufsOf(absGated) - 10.0;
    sum = 0.0;
    n = 0;
    for (double e : blockEnergy) {
        if (lufsOf(e) > relThresh && lufsOf(e) > -70.0) {
            sum += e;
            ++n;
        }
    }
    const double gated = n > 0 ? sum / double(n) : absGated;

    result.ok = true;
    result.integratedLufs = lufsOf(gated);
    result.truePeakDb = 20.0 * std::log10(truePeak);
    return result;
}

namespace {

double lufsFromPower(double meanPower)
{
    return -0.691 + 10.0 * std::log10(meanPower + 1e-20);
}

double percentileOf(std::vector<double> v, double pct)
{
    std::sort(v.begin(), v.end());
    const double rank = pct / 100.0 * double(v.size() - 1);
    const size_t lo = size_t(rank);
    const size_t hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (v[hi] - v[lo]) * (rank - double(lo));
}

// Mean power of `window` consecutive 50 ms segments, one value per `step` segments.
std::vector<double> slidingPower(const std::vector<double> &segPower, int window, int step)
{
    std::vector<double> out;
    for (size_t i = 0; i + size_t(window) <= segPower.size(); i += size_t(step)) {
        double sum = 0.0;
        for (int k = 0; k < window; ++k)
            sum += segPower[i + size_t(k)];
        out.push_back(sum / window);
    }
    return out;
}

LoudnessCurve curveOf(const std::vector<double> &power, double firstCentre, double dt, int stride)
{
    LoudnessCurve c;
    c.t0 = firstCentre;
    c.dt = dt;
    for (size_t i = 0; i < power.size(); i += size_t(stride))
        c.lufs.append(float(std::max(lufsFromPower(power[i]), -70.0)));
    return c;
}

} // namespace

LoudnessReport analyzeLoudness(qint64 totalFrames, int sampleRate,
                               const MediaWaveform::FillChunk &fill)
{
    LoudnessReport report;
    if (totalFrames <= 0 || sampleRate <= 0 || !fill)
        return report;
    report.durationSeconds = double(totalFrames) / double(sampleRate);

    // Everything below is built from 50 ms segments of summed K-weighted power, per channel
    // summed as BS.1770 does: 0.1 s, 0.4 s and 3 s windows are 2, 8 and 60 of them.
    const int seg = qMax(1, sampleRate / 20);
    Biquad hp[2] = {makeKHighPass(sampleRate), makeKHighPass(sampleRate)};
    Biquad sh[2] = {makeKShelf(sampleRate), makeKShelf(sampleRate)};
    TruePeakFollower peak[2];

    std::vector<double> segPower;
    std::vector<double> segSamplePeak; // per 50 ms, for the gap kind
    double acc = 0.0;
    double segPk = 0.0;
    int inSeg = 0;
    double truePeak = 0.0;
    double samplePeak = 0.0;
    int clipRun = 0;
    qint64 clipStart = 0;

    QVector<float> chunk(static_cast<qsizetype>(kChunkFrames) * 2);
    qint64 done = 0;
    const auto closeRun = [&]() {
        if (clipRun >= 3)
            report.clippingRuns.append({double(clipStart) / sampleRate, clipRun});
        clipRun = 0;
    };
    while (done < totalFrames) {
        const int want = static_cast<int>(qMin<qint64>(kChunkFrames, totalFrames - done));
        const int got = fill(chunk.data(), done, want);
        if (got <= 0)
            break;
        for (int i = 0; i < got; ++i) {
            bool clipped = false;
            for (int ch = 0; ch < 2; ++ch) {
                const double x = chunk[i * 2 + ch];
                const double a = std::abs(x);
                samplePeak = std::max(samplePeak, a);
                segPk = std::max(segPk, a);
                if (a >= 0.999)
                    clipped = true;
                truePeak = std::max(truePeak, peak[ch].push(x));
                const double k = sh[ch].process(hp[ch].process(x));
                acc += k * k;
            }
            if (clipped) {
                ++report.clippedSamples;
                if (clipRun == 0)
                    clipStart = done + i;
                ++clipRun;
            } else if (clipRun > 0) {
                closeRun();
            }
            if (++inSeg == seg) {
                segPower.push_back(acc / seg);
                segSamplePeak.push_back(segPk);
                acc = 0.0;
                segPk = 0.0;
                inSeg = 0;
            }
        }
        done += got;
    }
    closeRun();
    if (inSeg > 0) {
        segPower.push_back(acc / inSeg);
        segSamplePeak.push_back(segPk);
    }

    report.ok = true;
    report.samplePeakDb = samplePeak > 0.0 ? 20.0 * std::log10(samplePeak) : -120.0;
    report.hasTruePeak = samplePeak > 0.0;
    report.truePeakDb = truePeak > 0.0 ? 20.0 * std::log10(truePeak) : -120.0;
    const double dur = report.durationSeconds;

    // Momentary: 0.4 s windows every 0.1 s.
    const std::vector<double> mom = slidingPower(segPower, 8, 2);
    if (dur >= 0.4 && !mom.empty()) {
        report.momentary = curveOf(mom, 0.2, 0.1, 1);
        const auto it = std::max_element(report.momentary.lufs.begin(), report.momentary.lufs.end());
        report.maxMomentaryLufs = *it;
        report.maxMomentaryAt = report.momentary.t0 + report.momentary.dt * double(it - report.momentary.lufs.begin());

        std::vector<double> gated;
        for (double p : mom) {
            if (lufsFromPower(p) > -70.0)
                gated.push_back(p);
        }
        if (!gated.empty()) {
            double mean = 0.0;
            for (double p : gated)
                mean += p;
            mean /= double(gated.size());
            const double rel = lufsFromPower(mean) - 10.0;
            double sum = 0.0;
            int n = 0;
            for (double p : gated) {
                if (lufsFromPower(p) > rel) {
                    sum += p;
                    ++n;
                }
            }
            report.hasIntegrated = true;
            report.integratedLufs = n > 0 ? lufsFromPower(sum / n) : rel + 10.0;
        }
    }

    // Short-term: 3 s windows; the curve is every 0.5 s, loudness range reads every 0.1 s.
    const std::vector<double> st = slidingPower(segPower, 60, 2);
    if (dur >= 3.0 && !st.empty()) {
        report.shortTerm = curveOf(st, 1.5, 0.5, 5);
        report.maxShortTermLufs = *std::max_element(report.shortTerm.lufs.begin(), report.shortTerm.lufs.end());
        if (dur >= 6.0) {
            std::vector<double> s;
            for (double p : st) {
                if (lufsFromPower(p) > -70.0)
                    s.push_back(p);
            }
            if (s.size() >= 5) {
                double mean = 0.0;
                for (double p : s)
                    mean += p;
                const double rel = lufsFromPower(mean / double(s.size())) - 20.0;
                std::vector<double> levels;
                for (double p : s) {
                    if (lufsFromPower(p) > rel)
                        levels.push_back(lufsFromPower(p));
                }
                if (levels.size() > 4) {
                    report.hasLra = true;
                    report.lraLu = percentileOf(levels, 95.0) - percentileOf(levels, 10.0);
                }
            }
        }
    }

    // Gaps: the fast (0.1 s) loudness, a window every 50 ms.
    const std::vector<double> fast = slidingPower(segPower, 2, 1);
    if (dur >= 0.5 && fast.size() >= 5) {
        const int n = int(fast.size());
        std::vector<double> fl(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            fl[size_t(i)] = std::max(lufsFromPower(fast[size_t(i)]), -70.0);
        const double thr = !report.hasIntegrated || report.integratedLufs <= -60.0
                               ? -70.0
                               : std::max(-60.0, report.integratedLufs - 20.0);
        std::vector<char> low(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            low[size_t(i)] = report.hasIntegrated ? fl[size_t(i)] < thr : fl[size_t(i)] <= thr;
        // Close one-frame holes: a dilation then an erosion over 7 frames, edges replicated.
        constexpr int kHalf = 3;
        const int padded = n + 2 * kHalf;
        std::vector<char> pad(static_cast<size_t>(padded)), dil(static_cast<size_t>(padded), 0), ero(static_cast<size_t>(padded), 0);
        for (int i = 0; i < padded; ++i)
            pad[size_t(i)] = low[size_t(std::clamp(i - kHalf, 0, n - 1))];
        for (int i = 0; i < padded; ++i) {
            for (int k = -kHalf; k <= kHalf; ++k) {
                if (i + k >= 0 && i + k < padded && pad[size_t(i + k)]) {
                    dil[size_t(i)] = 1;
                    break;
                }
            }
        }
        for (int i = 0; i < padded; ++i) {
            bool all = true;
            for (int k = -kHalf; k <= kHalf && all; ++k)
                all = i + k >= 0 && i + k < padded && dil[size_t(i + k)];
            ero[size_t(i)] = all;
        }
        for (int i = 0; i < n; ++i)
            low[size_t(i)] = ero[size_t(i + kHalf)] && fl[size_t(i)] < thr + 6.0;

        for (int s = 0; s < n;) {
            if (!low[size_t(s)]) {
                ++s;
                continue;
            }
            int e = s;
            while (e < n && low[size_t(e)])
                ++e;
            if (e - s >= 5) {
                double a = std::max(0.0, s * 0.05);
                double b = std::min(dur, (e - 1) * 0.05 + 0.1);
                if (s == 0)
                    a = 0.0;
                if (e == n)
                    b = dur;
                double pk = 0.0;
                const size_t i0 = size_t(a * 20.0);
                const size_t i1 = std::min(segSamplePeak.size(), size_t(std::ceil(b * 20.0)));
                for (size_t i = i0; i < i1; ++i)
                    pk = std::max(pk, segSamplePeak[i]);
                double fmax = -70.0;
                double fsum = 0.0;
                for (int i = s; i < e; ++i) {
                    fmax = std::max(fmax, fl[size_t(i)]);
                    fsum += fl[size_t(i)];
                }
                LoudnessGap g;
                g.start = a;
                g.end = b;
                g.levelLufs = fsum / (e - s);
                g.kind = pk < 1e-5 ? QStringLiteral("digital silence")
                                   : (fmax < -60.0 ? QStringLiteral("near silence") : QStringLiteral("drop"));
                const bool head = a <= 0.05;
                const bool tail = b >= dur - 0.05;
                if (head || tail)
                    g.where = head && tail ? QStringLiteral("whole file")
                                           : (head ? QStringLiteral("lead-in") : QStringLiteral("tail"));
                report.gaps.append(g);
            }
            s = e;
        }
    }
    return report;
}

struct RealtimeLoudnessMeter::State
{
    mutable QMutex mutex;
    int sampleRate = 48000;
    int hopFrames = 4800; // 100 ms

    Biquad hpL;
    Biquad hpR;
    Biquad shL;
    Biquad shR;
    TruePeakFollower peakL;
    TruePeakFollower peakR;

    double currentHopEnergy = 0.0;
    int currentHopSamples = 0;

    // 100ms hop mean-square powers: last 4 hops = 400ms Momentary, last 30 hops = 3s Short-Term.
    std::array<double, 30> hopRing{};
    int hopCount = 0;
    int hopCursor = 0;

    // 400ms overlapping block energies for EBU R128 gated Integrated LUFS (capped at ~1 hour).
    QVector<double> gatedBlocks;
    // 3s short-term energies for approximate LRA.
    QVector<double> shortTermBlocks;

    double lastTruePeakDbfs = -120.0;
    double maxHoldTruePeakDbfs = -120.0;
    bool clipping = false;
    RealtimeLoudnessSnapshot cachedSnapshot;

    static double powerToLufs(double meanSquare)
    {
        if (meanSquare <= 1e-12)
            return -70.0;
        return -0.691 + 10.0 * std::log10(meanSquare);
    }

    static double linearToDbfs(double amp)
    {
        if (amp <= 1e-6)
            return -120.0;
        return 20.0 * std::log10(amp);
    }

    void configureFilters(int sr)
    {
        sampleRate = qMax(8000, sr);
        hopFrames = qMax(1, sampleRate / 10);
        hpL = makeKHighPass(sampleRate);
        hpR = makeKHighPass(sampleRate);
        shL = makeKShelf(sampleRate);
        shR = makeKShelf(sampleRate);
    }

    void resetAll()
    {
        configureFilters(sampleRate);
        peakL = TruePeakFollower{};
        peakR = TruePeakFollower{};
        currentHopEnergy = 0.0;
        currentHopSamples = 0;
        hopRing.fill(0.0);
        hopCount = 0;
        hopCursor = 0;
        gatedBlocks.clear();
        shortTermBlocks.clear();
        lastTruePeakDbfs = -120.0;
        maxHoldTruePeakDbfs = -120.0;
        clipping = false;
        cachedSnapshot = RealtimeLoudnessSnapshot{};
    }

    void updateSnapshotMetrics(double chunkTruePeakDbfs, bool chunkClipping)
    {
        if (hopCount > 0) {
            const int mCount = qMin(4, hopCount);
            double mSum = 0.0;
            for (int i = 0; i < mCount; ++i) {
                const int idx = (hopCursor - 1 - i + 30) % 30;
                mSum += hopRing[static_cast<size_t>(idx)];
            }
            cachedSnapshot.momentaryLufs = powerToLufs(mSum / double(mCount));

            const int sCount = qMin(30, hopCount);
            double sSum = 0.0;
            for (int i = 0; i < sCount; ++i) {
                const int idx = (hopCursor - 1 - i + 30) % 30;
                sSum += hopRing[static_cast<size_t>(idx)];
            }
            cachedSnapshot.shortTermLufs = powerToLufs(sSum / double(sCount));
        }

        if (!gatedBlocks.isEmpty()) {
            double absSum = 0.0;
            int absN = 0;
            for (double e : std::as_const(gatedBlocks)) {
                if (powerToLufs(e) > -70.0) {
                    absSum += e;
                    ++absN;
                }
            }
            if (absN > 0) {
                const double relThresh = powerToLufs(absSum / double(absN)) - 10.0;
                double relSum = 0.0;
                int relN = 0;
                for (double e : std::as_const(gatedBlocks)) {
                    if (powerToLufs(e) > relThresh && powerToLufs(e) > -70.0) {
                        relSum += e;
                        ++relN;
                    }
                }
                cachedSnapshot.integratedLufs =
                    relN > 0 ? powerToLufs(relSum / double(relN)) : -70.0;
            } else {
                cachedSnapshot.integratedLufs = -70.0;
            }
        }

        if (shortTermBlocks.size() >= 6) {
            QVector<double> absGated;
            double sum = 0.0;
            for (double e : std::as_const(shortTermBlocks)) {
                if (powerToLufs(e) > -70.0) {
                    absGated.append(e);
                    sum += e;
                }
            }
            if (!absGated.isEmpty()) {
                const double rel = powerToLufs(sum / double(absGated.size())) - 20.0;
                QVector<double> kept;
                for (double e : absGated) {
                    const double l = powerToLufs(e);
                    if (l > rel)
                        kept.append(l);
                }
                if (kept.size() >= 2) {
                    std::sort(kept.begin(), kept.end());
                    const int i10 = qBound(0, int(std::floor(0.10 * (kept.size() - 1))), kept.size() - 1);
                    const int i95 = qBound(0, int(std::floor(0.95 * (kept.size() - 1))), kept.size() - 1);
                    cachedSnapshot.loudnessRangeLu = qMax(0.0, kept.at(i95) - kept.at(i10));
                }
            }
        }

        lastTruePeakDbfs = chunkTruePeakDbfs;
        maxHoldTruePeakDbfs = qMax(maxHoldTruePeakDbfs, chunkTruePeakDbfs);
        clipping = clipping || chunkClipping || chunkTruePeakDbfs > 0.0;
        cachedSnapshot.truePeakDbfs = lastTruePeakDbfs;
        cachedSnapshot.maxHoldTruePeakDbfs = maxHoldTruePeakDbfs;
        cachedSnapshot.clipping = clipping;
    }
};

RealtimeLoudnessMeter::RealtimeLoudnessMeter(int sampleRate)
    : m_state(new State)
{
    m_state->sampleRate = qMax(8000, sampleRate);
    m_state->resetAll();
}

RealtimeLoudnessMeter::~RealtimeLoudnessMeter()
{
    delete m_state;
}

void RealtimeLoudnessMeter::setSampleRate(int sampleRate)
{
    const int sr = qMax(8000, sampleRate);
    QMutexLocker locker(&m_state->mutex);
    if (m_state->sampleRate == sr)
        return;
    m_state->sampleRate = sr;
    m_state->resetAll();
}

void RealtimeLoudnessMeter::reset()
{
    QMutexLocker locker(&m_state->mutex);
    m_state->resetAll();
}

void RealtimeLoudnessMeter::processInterleavedStereo(const float *samples, int frameCount)
{
    if (!samples || frameCount <= 0)
        return;

    QMutexLocker locker(&m_state->mutex);
    double chunkPeakAmp = 0.0;
    bool chunkClipping = false;
    for (int i = 0; i < frameCount; ++i) {
        const double l = samples[i * 2];
        const double r = samples[i * 2 + 1];
        if (qAbs(l) >= 0.999 || qAbs(r) >= 0.999)
            chunkClipping = true;
        chunkPeakAmp = qMax(chunkPeakAmp, m_state->peakL.push(l));
        chunkPeakAmp = qMax(chunkPeakAmp, m_state->peakR.push(r));

        const double kl = m_state->shL.process(m_state->hpL.process(l));
        const double kr = m_state->shR.process(m_state->hpR.process(r));
        m_state->currentHopEnergy += kl * kl + kr * kr;
        ++m_state->currentHopSamples;

        if (m_state->currentHopSamples >= m_state->hopFrames) {
            const double hopPower =
                m_state->currentHopEnergy / double(m_state->currentHopSamples);
            m_state->hopRing[static_cast<size_t>(m_state->hopCursor)] = hopPower;
            m_state->hopCursor = (m_state->hopCursor + 1) % 30;
            m_state->hopCount = qMin(m_state->hopCount + 1, 30);
            m_state->currentHopEnergy = 0.0;
            m_state->currentHopSamples = 0;

            if (m_state->hopCount >= 4) {
                double mSum = 0.0;
                for (int h = 0; h < 4; ++h) {
                    const int idx = (m_state->hopCursor - 1 - h + 30) % 30;
                    mSum += m_state->hopRing[static_cast<size_t>(idx)];
                }
                constexpr int kMaxGatedBlocks = 36000;
                if (m_state->gatedBlocks.size() >= kMaxGatedBlocks)
                    m_state->gatedBlocks.remove(0, kMaxGatedBlocks / 4);
                m_state->gatedBlocks.append(mSum / 4.0);
            }
            if (m_state->hopCount >= 30 && (m_state->gatedBlocks.size() % 5 == 0)) {
                double sSum = 0.0;
                for (int h = 0; h < 30; ++h)
                    sSum += m_state->hopRing[static_cast<size_t>(h)];
                constexpr int kMaxShortTermBlocks = 7200;
                if (m_state->shortTermBlocks.size() >= kMaxShortTermBlocks)
                    m_state->shortTermBlocks.remove(0, kMaxShortTermBlocks / 4);
                m_state->shortTermBlocks.append(sSum / 30.0);
            }
        }
    }

    // If a short test buffer didn't complete a full 100ms hop yet, still update momentary
    // fallback from the partial hop so short renders register energy too.
    if (m_state->hopCount == 0 && m_state->currentHopSamples > 0) {
        const double partialPower =
            m_state->currentHopEnergy / double(m_state->currentHopSamples);
        m_state->cachedSnapshot.momentaryLufs = State::powerToLufs(partialPower);
        m_state->cachedSnapshot.shortTermLufs = m_state->cachedSnapshot.momentaryLufs;
        if (State::powerToLufs(partialPower) > -70.0 && m_state->gatedBlocks.isEmpty())
            m_state->cachedSnapshot.integratedLufs = State::powerToLufs(partialPower);
    }

    m_state->updateSnapshotMetrics(State::linearToDbfs(chunkPeakAmp), chunkClipping);
}

RealtimeLoudnessSnapshot RealtimeLoudnessMeter::snapshot() const
{
    QMutexLocker locker(&m_state->mutex);
    return m_state->cachedSnapshot;
}

} // namespace drift
