/*
 * AWclean - automatic output gain
 *
 * Matches the loudness of the effect's output to its input, so turning an
 * effect on, or turning its drive up, doesn't fool your ears by being louder.
 *
 * Loudness is measured the same way an LUFS meter does it (ITU-R BS.1770
 * K-weighting, mean square per channel) on both the input and the output over
 * a sliding window of a couple of seconds. Because both measurements lag the
 * same way, their ratio is steady even while the music's level moves, so the
 * correction rides slowly like a careful engineer and doesn't pump.
 *
 * While the input is near silence the gain holds (reverb and delay tails keep
 * the level they had). Lock freezes the current gain so nothing moves at all.
 *
 * This source released under the MIT License, found in ~/LICENSE.md.
 */

#ifndef AWCLEAN_AUTOGAIN_H
#define AWCLEAN_AUTOGAIN_H

#include <algorithm>
#include <atomic>
#include <cmath>

namespace awclean
{
struct AutoGain
{
    struct Biquad
    {
        double b0{1}, b1{0}, b2{0}, a1{0}, a2{0};
        double z1{0}, z2{0};
        inline double step(double x)
        {
            // transposed direct form II
            auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void clear() { z1 = z2 = 0; }
    };

    // K-weighting for any sample rate (the BS.1770 analog prototypes, as libebur128 does it)
    struct KWeight
    {
        Biquad shelf, hpf;
        void setSampleRate(double fs)
        {
            {
                double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
                double K = std::tan(M_PI * f0 / fs);
                double Vh = std::pow(10.0, G / 20.0);
                double Vb = std::pow(Vh, 0.4996667741545416);
                double a0 = 1.0 + K / Q + K * K;
                shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
                shelf.b1 = 2.0 * (K * K - Vh) / a0;
                shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
                shelf.a1 = 2.0 * (K * K - 1.0) / a0;
                shelf.a2 = (1.0 - K / Q + K * K) / a0;
            }
            {
                double f0 = 38.13547087602444, Q = 0.5003270373238773;
                double K = std::tan(M_PI * f0 / fs);
                double a0 = 1.0 + K / Q + K * K;
                hpf.b0 = 1.0;
                hpf.b1 = -2.0;
                hpf.b2 = 1.0;
                hpf.a1 = 2.0 * (K * K - 1.0) / a0;
                hpf.a2 = (1.0 - K / Q + K * K) / a0;
            }
            clear();
        }
        void clear()
        {
            shelf.clear();
            hpf.clear();
        }
        inline double step(double x) { return hpf.step(shelf.step(x)); }
    };

    static constexpr int maxCh = 2;
    static constexpr double windowSeconds = 2.0, gainSeconds = 0.25;
    static constexpr double maxCorrectionDb = 24.0;
    static constexpr double silenceMeanSquare = 1e-7; // about -70 LUFS

    KWeight inK[maxCh], outK[maxCh];
    double inMS{0}, outMS{0};
    double msCoef{0}, gainCoef{0};
    double gain{1}, target{1};
    bool primed{false};

    std::atomic<bool> locked{false};
    std::atomic<float> appliedDb{0.f}; // for the UI

    void setSampleRate(double fs)
    {
        for (int c = 0; c < maxCh; ++c)
        {
            inK[c].setSampleRate(fs);
            outK[c].setSampleRate(fs);
        }
        msCoef = std::exp(-1.0 / (windowSeconds * fs));
        gainCoef = std::exp(-1.0 / (gainSeconds * fs));
        reset();
    }

    // Forget the loudness history, e.g. when the effect changes. Keeps the gain unless asked.
    void reset(bool resetGain = false)
    {
        for (int c = 0; c < maxCh; ++c)
        {
            inK[c].clear();
            outK[c].clear();
        }
        inMS = outMS = 0;
        primed = false;
        if (resetGain)
        {
            gain = target = 1;
            appliedDb = 0.f;
        }
    }

    // restores a locked gain from a saved session
    void setGainDb(double db)
    {
        gain = target = std::pow(10.0, std::clamp(db, -maxCorrectionDb, maxCorrectionDb) / 20.0);
        appliedDb = (float)db;
    }

    template <typename T> double blockMeanSquare(KWeight *k, T *const *ch, int nCh, int n, int i)
    {
        double s{0};
        for (int c = 0; c < nCh && c < maxCh; ++c)
        {
            auto v = k[c].step((double)ch[c][i]);
            s += v * v;
        }
        return s / nCh;
    }

    // Call with the dry input before the effect runs. Buffers may be the same memory as the
    // output later, so the input has to be measured first.
    template <typename T> void measureInput(const T *const *ch, int nCh, int n)
    {
        for (int i = 0; i < n; ++i)
            inMS = msCoef * inMS + (1 - msCoef) * blockMeanSquare(inK, ch, nCh, n, i);
    }

    // Call with the effect's output; measures it, then applies the correction in place.
    template <typename T> void processOutput(T *const *ch, int nCh, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            outMS = msCoef * outMS + (1 - msCoef) * blockMeanSquare(outK, ch, nCh, n, i);

            if (!locked && inMS > silenceMeanSquare && outMS > silenceMeanSquare * 1e-3)
            {
                auto t = std::sqrt(inMS / outMS);
                static const double lim = std::pow(10.0, maxCorrectionDb / 20.0);
                target = std::clamp(t, 1.0 / lim, lim);
                primed = true;
            }
            if (primed && !locked)
                gain = gainCoef * gain + (1 - gainCoef) * target;

            for (int c = 0; c < nCh; ++c)
                ch[c][i] = (T)(ch[c][i] * gain);
        }
        appliedDb = (float)(20.0 * std::log10(gain));
    }
};
} // namespace awclean
#endif
