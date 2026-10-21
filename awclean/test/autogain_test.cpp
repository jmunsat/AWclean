// Runs real Airwindows effects on a test mix with AWclean auto gain on, and reports how far
// the corrected output's integrated loudness (BS.1770 gated LUFS) lands from the input's.
// Build (after a cmake build of airwin-registry):
//   c++ -std=c++17 -O2 -I src -I src-juce awclean/test/autogain_test.cpp build/libairwin-registry.a
#include <cstdio>
#include <random>
#include <vector>
#include "AirwinRegistry.h"
#include "AWCleanAutoGain.h"

static double lufs(const std::vector<float> &L, const std::vector<float> &R, double fs, size_t from)
{
    awclean::AutoGain::KWeight kl, kr;
    kl.setSampleRate(fs);
    kr.setSampleRate(fs);
    std::vector<double> kL(L.size()), kR(R.size());
    for (size_t i = 0; i < L.size(); ++i)
    {
        kL[i] = kl.step(L[i]);
        kR[i] = kr.step(R[i]);
    }
    // 400 ms blocks, 75% overlap, absolute gate -70, relative gate -10
    size_t blk = (size_t)(0.4 * fs), hop = blk / 4;
    std::vector<double> z;
    for (size_t s = from; s + blk <= L.size(); s += hop)
    {
        double a = 0;
        for (size_t i = s; i < s + blk; ++i)
            a += kL[i] * kL[i] + kR[i] * kR[i];
        z.push_back(a / blk);
    }
    auto loud = [](double m) { return -0.691 + 10 * std::log10(m); };
    double sum = 0;
    int n = 0;
    for (auto m : z)
        if (loud(m) > -70)
            sum += m, n++;
    double rel = loud(sum / n) - 10;
    sum = 0;
    n = 0;
    for (auto m : z)
        if (loud(m) > -70 && loud(m) > rel)
            sum += m, n++;
    return loud(sum / n);
}

int main()
{
    const double fs = 48000;
    const int seconds = 30, N = (int)(fs * seconds), blockSize = 256;
    std::mt19937 rng(1);
    std::normal_distribution<float> nd(0, 1);

    // a test mix: kick-like thumps, hat-like noise bursts and a sustained chord
    std::vector<float> inL(N), inR(N);
    for (int i = 0; i < N; ++i)
    {
        double t = i / fs, beat = std::fmod(t, 0.5), off = std::fmod(t + 0.25, 0.5);
        double kick = std::sin(2 * M_PI * (50 + 80 * std::exp(-beat * 30)) * beat) * std::exp(-beat * 8);
        double hat = nd(rng) * 0.15 * std::exp(-off * 60);
        double chord = 0.08 * (std::sin(2 * M_PI * 220 * t) + std::sin(2 * M_PI * 277.2 * t) +
                               std::sin(2 * M_PI * 329.6 * t));
        inL[i] = (float)(0.5 * kick + hat + chord);
        inR[i] = (float)(0.5 * kick + 0.8 * hat + chord);
    }

    struct Case
    {
        const char *name;
        std::vector<float> params;
    };
    std::vector<Case> cases{
        {"Pressure5", {0.9f, 0.5f, 1.f, 0.5f, 0.5f, 1.f}},  // heavy bus compression
        {"Channel9", {0.f, 1.f, 1.f}},                      // console drive flat out
        {"Galactic", {0.5f, 0.5f, 0.5f, 1.f, 1.f}},         // big reverb, full wet
        {"Mackity", {1.f, 1.f}},                            // aggressive distortion
        {"Air3", {1.f, 0.5f}},                              // big air boost
        {"Capacitor2", {0.2f, 0.9f, 0.f, 0.5f, 1.f, 1.f}},  // dark lowpass
    };

    std::printf("%-12s %12s %14s %14s %10s\n", "effect", "input", "raw output", "auto gain", "error");
    int fails = 0;
    for (auto &c : cases)
    {
        auto it = AirwinRegistry::nameToIndex.find(c.name);
        if (it == AirwinRegistry::nameToIndex.end())
        {
            std::printf("%s missing\n", c.name);
            continue;
        }
        auto fx = AirwinRegistry::registry[it->second].generator();
        fx->setSampleRate(fs);
        for (size_t p = 0; p < c.params.size() && p < (size_t)AirwinRegistry::registry[it->second].nParams; ++p)
            fx->setParameter((int)p, c.params[p]);

        awclean::AutoGain ag;
        float gmin = 1000, gmax = -1000;
        ag.setSampleRate(fs);

        std::vector<float> rawL(N), rawR(N), outL(N), outR(N);
        std::vector<float> bl(blockSize), br(blockSize), il(blockSize), ir(blockSize);
        for (int s = 0; s < N; s += blockSize)
        {
            int n = std::min(blockSize, N - s);
            std::copy(inL.begin() + s, inL.begin() + s + n, il.begin());
            std::copy(inR.begin() + s, inR.begin() + s + n, ir.begin());
            const float *inp[2]{il.data(), ir.data()};
            ag.measureInput(inp, 2, n);
            float *ins[2]{il.data(), ir.data()};
            float *outs[2]{bl.data(), br.data()};
            fx->processReplacing(ins, outs, n);
            std::copy(bl.begin(), bl.begin() + n, rawL.begin() + s);
            std::copy(br.begin(), br.begin() + n, rawR.begin() + s);
            ag.processOutput(outs, 2, n);
            if (s > 10 * fs)
            {
                gmin = std::min(gmin, ag.appliedDb.load());
                gmax = std::max(gmax, ag.appliedDb.load());
            }
            std::copy(bl.begin(), bl.begin() + n, outL.begin() + s);
            std::copy(br.begin(), br.begin() + n, outR.begin() + s);
        }
        size_t from = (size_t)(10 * fs); // judge after the first 10 s settle
        auto li = lufs(inL, inR, fs, from), lr = lufs(rawL, rawR, fs, from),
             lo = lufs(outL, outR, fs, from);
        std::printf("%-12s %8.2f LUFS %8.2f LUFS %8.2f LUFS %+7.2f dB  (gain moved %.2f dB)\n", c.name, li, lr, lo, lo - li, gmax - gmin);
        if (std::fabs(lo - li) > 0.5)
            fails++;
    }
    std::printf("%s\n", fails ? "SOME CASES OFF BY MORE THAN 0.5 dB" : "all within 0.5 dB");
    return fails ? 1 : 0;
}
