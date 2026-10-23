#include "StyleProfile.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

namespace icc
{

const char* laneName (int lane) noexcept
{
    static const char* n[kLanes] = { "Kick", "Snare", "Hat closed", "Hat open", "Cymbal", "Tom", "Perc" };
    return n[std::clamp (lane, 0, kLanes - 1)];
}

int laneOfKey (int k) noexcept
{
    switch (k)
    {
        case 35: case 36: return LKick;
        case 37: case 38: case 39: case 40: return LSnare;
        case 42: case 44: return LHatClosed;
        case 46: return LHatOpen;
        case 49: case 51: case 52: case 53: case 55: case 57: case 59: return LCymbal;
        case 41: case 43: case 45: case 47: case 48: case 50: return LTom;
        default: return LPerc;
    }
}

int laneDefaultKey (int lane) noexcept
{
    static const int k[kLanes] = { 36, 38, 42, 46, 51, 45, 54 };
    return k[std::clamp (lane, 0, kLanes - 1)];
}

int StyleProfile::pickKey (int lane, Rng& rng) const
{
    const auto& d = pitchDist[(size_t) lane];
    float total = 0.0f;
    for (float v : d) total += v;
    if (total <= 1e-6f) return laneDefaultKey (lane);
    float r = rng.nextFloat() * total;
    for (int k = 0; k < 128; ++k)
    {
        r -= d[(size_t) k];
        if (r < 0.0f) return k;
    }
    return laneDefaultKey (lane);
}

// ------------------------------------------------------------------ analysis
StyleProfile profileOfDrums (const std::vector<Note>& notes, int barsIn, const std::string& name)
{
    StyleProfile p;
    p.name = name;
    const int bars = std::clamp (barsIn, 1, 256);

    std::vector<std::array<std::array<bool, 16>, kLanes>> hit ((size_t) bars);
    std::vector<int> barHits ((size_t) bars, 0);
    for (auto& b : hit) for (auto& l : b) l.fill (false);

    std::array<double, kLanes> velSum {}, velSq {}, cnt {};
    std::array<std::array<double, 128>, kLanes> pitch {};
    double oddOffsetSum = 0; int oddCount = 0, offGrid = 0, total = 0;

    for (const auto& n : notes)
    {
        if (n.start < 0) continue;
        const int bar = n.start / kBarTicks;
        if (bar >= bars) continue;
        const int inBar = n.start - bar * kBarTicks;
        int step = (int) std::lround ((double) inBar / 240.0);
        const int offset = inBar - step * 240;
        if (step >= 16) step = 0;
        const int lane = laneOfKey (n.pitch);
        ++barHits[(size_t) bar]; ++total;
        velSum[(size_t) lane] += n.velocity; velSq[(size_t) lane] += (double) n.velocity * n.velocity; cnt[(size_t) lane] += 1.0;
        pitch[(size_t) lane][(size_t) std::clamp (n.pitch, 0, 127)] += 1.0;
        if (std::abs (offset) <= 100)
        {
            hit[(size_t) bar][(size_t) lane][(size_t) step] = true;
            if ((step & 1) && lane != LKick) { oddOffsetSum += offset; ++oddCount; }
        }
        else ++offGrid;
    }

    int counted = 0;
    for (int b = 0; b < bars; ++b)
    {
        if (barHits[(size_t) b] == 0) continue;
        ++counted;
        for (int l = 0; l < kLanes; ++l)
            for (int s = 0; s < 16; ++s)
                if (hit[(size_t) b][(size_t) l][(size_t) s]) p.prob[(size_t) l][(size_t) s] += 1.0f;
        p.densityMean += (float) barHits[(size_t) b];
    }
    p.bars = counted;
    p.files = counted > 0 ? 1 : 0;
    if (counted == 0) return p;

    for (int l = 0; l < kLanes; ++l)
    {
        for (auto& v : p.prob[(size_t) l]) v /= (float) counted;
        const double c = cnt[(size_t) l];
        p.hitsPerBar[(size_t) l] = (float) (c / counted);
        if (c > 0)
        {
            const double m = velSum[(size_t) l] / c;
            p.velMean[(size_t) l] = (float) m;
            p.velStd[(size_t) l]  = (float) std::sqrt (std::max (0.0, velSq[(size_t) l] / c - m * m));
            for (int k = 0; k < 128; ++k) p.pitchDist[(size_t) l][(size_t) k] = (float) (pitch[(size_t) l][(size_t) k] / c);
        }
    }
    p.densityMean /= (float) counted;
    p.densityP05 = p.densityP95 = p.densityMean;
    p.offGrid = total > 0 ? (float) offGrid / (float) total : 0.0f;
    p.swing = oddCount >= 4 ? std::clamp (0.5f + (float) (oddOffsetSum / oddCount) / 480.0f, 0.5f, 0.75f) : 0.5f;
    return p;
}

// ------------------------------------------------------------------ trainer
namespace
{
    float percentile (std::vector<float> v, double q)
    {
        if (v.empty()) return 0.0f;
        std::sort (v.begin(), v.end());
        return v[(size_t) std::lround (q * (double) (v.size() - 1))];
    }
}

void ProfileTrainer::add (const StyleProfile& s, double bpm)
{
    if (s.files <= 0 || s.bars <= 0) return;
    ++count;
    barsTotal += s.bars;
    swingSum += s.swing; offSum += s.offGrid;
    for (int l = 0; l < kLanes; ++l)
    {
        for (int k = 0; k < 16; ++k) probSum[l][k] += s.prob[(size_t) l][(size_t) k];
        hitsSum[l] += s.hitsPerBar[(size_t) l];
        if (s.hitsPerBar[(size_t) l] > 0.0f)
        {
            laneFiles[l] += 1.0;
            velMeanSum[l] += s.velMean[(size_t) l]; velStdSum[l] += s.velStd[(size_t) l];
            for (int k = 0; k < 128; ++k) pitchSum[l][k] += s.pitchDist[(size_t) l][(size_t) k];
        }
    }
    density.push_back (s.densityMean);
    if (bpm > 20.0 && bpm < 400.0) bpms.push_back ((float) bpm);
}

StyleProfile ProfileTrainer::finish (const std::string& name) const
{
    StyleProfile p;
    p.name = name; p.files = count; p.bars = (int) std::min<double> (barsTotal, 2e9);
    if (count == 0) return p;
    const double n = (double) count;
    for (int l = 0; l < kLanes; ++l)
    {
        for (int k = 0; k < 16; ++k) p.prob[(size_t) l][(size_t) k] = (float) (probSum[l][k] / n);
        p.hitsPerBar[(size_t) l] = (float) (hitsSum[l] / n);
        if (laneFiles[l] > 0)
        {
            p.velMean[(size_t) l] = (float) (velMeanSum[l] / laneFiles[l]);
            p.velStd[(size_t) l]  = (float) (velStdSum[l] / laneFiles[l]);
            double tot = 0;
            for (int k = 0; k < 128; ++k) tot += pitchSum[l][k];
            if (tot > 0) for (int k = 0; k < 128; ++k) p.pitchDist[(size_t) l][(size_t) k] = (float) (pitchSum[l][k] / tot);
        }
    }
    p.swing = (float) (swingSum / n);
    p.offGrid = (float) (offSum / n);
    float dm = 0; for (float d : density) dm += d;
    p.densityMean = dm / (float) density.size();
    p.densityP05 = percentile (density, 0.05); p.densityP95 = percentile (density, 0.95);
    if (! bpms.empty())
    {
        float bm = 0; for (float b : bpms) bm += b;
        p.bpmMean = bm / (float) bpms.size();
        p.bpmP05 = percentile (bpms, 0.05); p.bpmP95 = percentile (bpms, 0.95);
    }
    return p;
}

// ------------------------------------------------------------------ built-in profiles
const StyleProfile& builtinProfile (int index)
{
    static const std::vector<StyleProfile> all = []
    {
        std::vector<StyleProfile> v;
        for (int i = 0; i < drumStyleCount(); ++i)
        {
            ProfileTrainer t;
            for (int s = 0; s < 24; ++s)
            {
                Rng rng ((uint32_t) (1000 + s * 37 + i));
                const auto plan = makePhrasePlan (4, rng);
                DrumSettings ds; ds.style = i; ds.complexity = 0.5f; ds.swing = 0.0f; ds.humanize = 0.0f; ds.fills = 0.5f; ds.bars = 4;
                t.add (profileOfDrums (generateDrums (ds, plan, rng).notes, 4), drumStyleBpm (i));
            }
            v.push_back (t.finish (drumStyleName (i)));
        }
        return v;
    }();
    return all[(size_t) std::clamp (index, 0, (int) all.size() - 1)];
}

// ------------------------------------------------------------------ comparison
float profileDistance (const StyleProfile& a, const StyleProfile& b)
{
    double num = 0, den = 0, velD = 0, velN = 0;
    for (int l = 0; l < kLanes; ++l)
    {
        const double w = std::max (a.hitsPerBar[(size_t) l], b.hitsPerBar[(size_t) l]);
        if (w < 0.25) continue;
        double d = 0;
        for (int s = 0; s < 16; ++s) d += std::abs (a.prob[(size_t) l][(size_t) s] - b.prob[(size_t) l][(size_t) s]);
        num += w * d / 16.0; den += w;
        if (a.velMean[(size_t) l] > 0 && b.velMean[(size_t) l] > 0) { velD += std::abs (a.velMean[(size_t) l] - b.velMean[(size_t) l]) / 127.0; velN += 1; }
    }
    const double g = den > 0 ? num / den : 1.0;
    const double dens = std::abs (a.densityMean - b.densityMean) / std::max (1.0f, std::max (a.densityMean, b.densityMean));
    const double vel = velN > 0 ? velD / velN : 0.0;
    const double sw = std::min (1.0, std::abs (a.swing - b.swing) / 0.25);
    return (float) std::clamp (0.7 * (g / 0.45) + 0.15 * dens + 0.1 * vel * 4.0 + 0.05 * sw, 0.0, 1.0);
}

float styleMatch (const std::vector<Note>& notes, int bars, const StyleProfile& t)
{
    if (t.files <= 0) return 0.0f;
    const auto me = profileOfDrums (notes, bars);
    if (me.files <= 0) return 0.0f;

    double num = 0, den = 0;
    for (int l = 0; l < kLanes; ++l)
    {
        const double w = std::max (me.hitsPerBar[(size_t) l], t.hitsPerBar[(size_t) l]);
        if (w < 0.25) continue;
        double l1 = 0, best = 0, worst = 0;
        for (int s = 0; s < 16; ++s)
        {
            const double p = t.prob[(size_t) l][(size_t) s], h = me.prob[(size_t) l][(size_t) s];
            l1 += std::abs (h - p); best += std::min (p, 1.0 - p); worst += std::max (p, 1.0 - p);
        }
        num += w * (1.0 - std::clamp ((l1 - best) / (worst - best + 1e-3), 0.0, 1.0)); den += w;
    }
    const double grid = den > 0 ? num / den : 0.0;
    const double dens = 1.0 - std::min (1.0, (double) std::abs (me.densityMean - t.densityMean) / (double) std::max (1.0f, t.densityMean));
    return (float) (100.0 * std::clamp (0.8 * grid + 0.2 * dens, 0.0, 1.0));
}

// ------------------------------------------------------------------ generation
namespace
{
    int jitterFor (int lane)
    {
        switch (lane) { case LKick: return 8; case LSnare: return 12; case LHatClosed: case LHatOpen: return 16; default: return 14; }
    }

    int stepTick (int bar, int step, float ratio)
    {
        const int base = bar * kBarTicks;
        return (step & 1) ? base + (step - 1) * 240 + (int) std::lround (480.0f * ratio) : base + step * 240;
    }

    int velFor (const StyleProfile& p, int lane, int step, Rng& rng)
    {
        const float mean = p.velMean[(size_t) lane] > 0.0f ? p.velMean[(size_t) lane] : 90.0f;
        float v = mean + (rng.nextFloat() * 2.0f - 1.0f) * p.velStd[(size_t) lane] * 0.7f;
        if (step % 4 == 0) v += 4.0f;
        return std::clamp ((int) std::lround (v), 1, 127);
    }
}

DrumResult generateProfileDrums (const StyleProfile& pf, const DrumSettings& s, const std::vector<PhrasePlan>& plan, Rng& rng)
{
    const int bars = std::clamp (s.bars, 1, kMaxBars);
    const float complexity = std::clamp (s.complexity, 0.0f, 1.0f);
    const float expo = 1.8f - 1.2f * complexity;
    const float ratio = std::clamp (std::max (pf.swing, 0.5f + std::clamp (s.swing, 0.0f, 1.0f) * 0.25f), 0.5f, 0.75f);
    const float hum = std::clamp (s.humanize, 0.0f, 1.0f);

    struct Hit { int tick, key, vel, lane; };
    std::vector<Hit> all;
    using Grid = std::array<std::array<bool, 16>, kLanes>;
    Grid base {};
    for (auto& l : base) l.fill (false);

    for (int bar = 0; bar < bars; ++bar)
    {
        const PhrasePlan& pp = plan.empty() ? PhrasePlan {} : planAtBar (plan, bar);
        const bool fresh = bar == 0 || isFirstBarOfPhrase (pp, bar);
        Grid g {};
        for (auto& l : g) l.fill (false);
        for (int l = 0; l < kLanes; ++l)
            for (int st = 0; st < 16; ++st)
            {
                const float p0 = pf.prob[(size_t) l][(size_t) st];
                float p = p0 >= 0.9f ? 1.0f : std::pow (p0, expo) * (0.8f + 0.2f * pp.activity);
                const float r1 = rng.nextFloat(), r2 = rng.nextFloat();
                bool h;
                if (p0 < 0.02f) h = false;
                else if (fresh) h = r1 < p;
                else if (p0 >= 0.9f) h = true;
                else h = r2 < 0.75f ? base[(size_t) l][(size_t) st] : r1 < p;
                g[(size_t) l][(size_t) st] = h;
            }
        if (fresh) base = g;
        for (int st = 0; st < 16; ++st)                                    // an open hat chokes the closed hat
            if (g[LHatOpen][(size_t) st]) g[LHatClosed][(size_t) st] = false;

        const bool fillBar = isLastBarOfPhrase (pp, bar) && bar > 0 && rng.nextFloat() < s.fills * 0.8f && pf.laneUsed (LSnare);
        for (int l = 0; l < kLanes; ++l)
            for (int st = 0; st < 16; ++st)
            {
                if (fillBar && st >= 12 && l != LSnare && l != LKick) continue;
                if (! g[(size_t) l][(size_t) st]) continue;
                all.push_back ({ stepTick (bar, st, ratio), pf.pickKey (l, rng), velFor (pf, l, st, rng), l });
            }
        if (fillBar)
            for (int st = 12; st < 16; ++st)
                all.push_back ({ stepTick (bar, st, ratio), pf.pickKey (LSnare, rng), 70 + (st - 12) * 12, LSnare });
    }

    DrumResult r;
    for (const auto& h : all)
    {
        int tick = h.tick, vel = h.vel;
        if (hum > 0.0f)
        {
            const int bar = h.tick / kBarTicks;
            tick = std::clamp (h.tick + (int) std::lround ((rng.nextFloat() * 2.0f - 1.0f) * hum * (float) jitterFor (h.lane)), bar * kBarTicks, bar * kBarTicks + kBarTicks - 1);
            vel = std::clamp (vel + (int) std::lround ((rng.nextFloat() * 2.0f - 1.0f) * hum * 10.0f), 1, 127);
        }
        r.notes.push_back ({ tick, 100, h.key, vel });
    }
    std::stable_sort (r.notes.begin(), r.notes.end(), [] (const Note& a, const Note& b)
                      { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
    r.notes.erase (std::unique (r.notes.begin(), r.notes.end(), [] (const Note& a, const Note& b) { return a.start == b.start && a.pitch == b.pitch; }), r.notes.end());

    const auto an = analyseDrums (r.notes, bars);
    r.anchors = an.anchors; r.kicks = an.kicks;
    r.style = 0; r.sustain = 0.6f;
    r.bassMode = BassMode::KickLock;
    float offbeatHat = 0.0f;
    for (int st : { 2, 6, 10, 14 }) offbeatHat += pf.prob[LHatOpen][(size_t) st] / 4.0f;
    if (pf.hitsPerBar[LKick] < 1.0f) r.bassMode = BassMode::Eighths;
    else if (pf.prob[LKick][0] > 0.8f && pf.prob[LKick][4] > 0.8f && pf.prob[LKick][8] > 0.8f && pf.prob[LKick][12] > 0.8f && offbeatHat > 0.5f) r.bassMode = BassMode::Offbeat;
    return r;
}

// ------------------------------------------------------------------ restyle
std::vector<Note> restyleDrums (const std::vector<Note>& in, int barsIn, const StyleProfile& t, float strengthIn, uint32_t seed)
{
    auto sorted = in;
    auto order = [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; };
    std::stable_sort (sorted.begin(), sorted.end(), order);
    const float strength = std::clamp (strengthIn, 0.0f, 1.0f);
    if (strength <= 0.0f || t.files <= 0) return sorted;

    const int bars = std::clamp (barsIn, 1, kMaxBars);
    Rng rng (seed ^ 0x5EEDu);
    const float curSwing = profileOfDrums (sorted, bars).swing;
    const float newSwing = std::clamp (curSwing + strength * (std::clamp (t.swing, 0.5f, 0.75f) - curSwing), 0.5f, 0.75f);
    const int swingShift = (int) std::lround (480.0f * (newSwing - curSwing));

    struct Item { Note n; int lane, bar, step; bool onGrid; bool keep; };
    std::vector<Item> items;
    std::vector<std::array<std::array<bool, 16>, kLanes>> has ((size_t) bars);
    for (auto& b : has) for (auto& l : b) l.fill (false);
    std::array<std::map<int, int>, kLanes> keyCount;
    std::array<double, kLanes> vsum {}, vcnt {};
    std::vector<int> perBar ((size_t) bars, 0);

    for (const auto& n : sorted)
    {
        if (n.start < 0 || n.start / kBarTicks >= bars) continue;
        const int bar = n.start / kBarTicks, inBar = n.start - bar * kBarTicks;
        int step = (int) std::lround ((double) inBar / 240.0);
        const int off = inBar - step * 240;
        if (step >= 16) step = 0;
        const int lane = laneOfKey (n.pitch);
        const bool on = std::abs (off) <= 100;
        items.push_back ({ n, lane, bar, step, on, true });
        if (on) has[(size_t) bar][(size_t) lane][(size_t) step] = true;
        ++keyCount[(size_t) lane][n.pitch];
        vsum[(size_t) lane] += n.velocity; vcnt[(size_t) lane] += 1.0;
        ++perBar[(size_t) bar];
    }

    // thin hits the target rarely plays
    for (auto& it : items)
    {
        const float r = rng.nextFloat();
        if (! it.onGrid) { if (t.offGrid < 0.01f && r < strength * 0.5f) it.keep = false; continue; }
        const float p = t.prob[(size_t) it.lane][(size_t) it.step];
        if (p < 0.5f && r < strength * (1.0f - 2.0f * p) * 0.9f) { it.keep = false; --perBar[(size_t) it.bar]; has[(size_t) it.bar][(size_t) it.lane][(size_t) it.step] = false; }
    }

    // velocity push per lane
    std::array<int, kLanes> velDelta {};
    for (int l = 0; l < kLanes; ++l)
        if (vcnt[(size_t) l] > 0 && t.velMean[(size_t) l] > 0.0f)
            velDelta[(size_t) l] = (int) std::lround (strength * (t.velMean[(size_t) l] - vsum[(size_t) l] / vcnt[(size_t) l]));

    std::vector<Note> out;
    for (const auto& it : items)
    {
        if (! it.keep) continue;
        Note n = it.n;
        n.velocity = std::clamp (n.velocity + velDelta[(size_t) it.lane], 1, 127);
        if (it.onGrid && (it.step & 1) && swingShift != 0)
            n.start = std::clamp (n.start + swingShift, it.bar * kBarTicks, it.bar * kBarTicks + kBarTicks - 1);
        out.push_back (n);
    }

    // add hits the target almost always plays
    const int cap = (int) std::max ({ t.densityP95, t.densityMean * 1.3f, 8.0f });
    for (int bar = 0; bar < bars; ++bar)
        for (int l = 0; l < kLanes; ++l)
        {
            int key = laneDefaultKey (l);
            int best = 0;
            for (auto& [k, c] : keyCount[(size_t) l]) if (c > best) { best = c; key = k; }
            const bool haveKey = best > 0;
            for (int st = 0; st < 16; ++st)
            {
                const float p = t.prob[(size_t) l][(size_t) st];
                const float r = rng.nextFloat();
                if (p <= 0.5f || has[(size_t) bar][(size_t) l][(size_t) st] || perBar[(size_t) bar] >= cap) continue;
                if (r >= strength * (2.0f * p - 1.0f) * 0.8f) continue;
                const int k = haveKey ? key : t.pickKey (l, rng);
                const float mean = t.velMean[(size_t) l] > 0.0f ? t.velMean[(size_t) l] : 90.0f;
                const int v = std::clamp ((int) std::lround (mean + (rng.nextFloat() * 2.0f - 1.0f) * t.velStd[(size_t) l] * 0.5f), 1, 127);
                out.push_back ({ stepTick (bar, st, newSwing), 100, k, v });
                ++perBar[(size_t) bar];
            }
        }

    std::stable_sort (out.begin(), out.end(), order);
    out.erase (std::unique (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.start == b.start && a.pitch == b.pitch; }), out.end());
    return out;
}

// ------------------------------------------------------------------ text
std::string describeProfile (const StyleProfile& p)
{
    static const char* shade = " .:-=+*#@";
    std::string s;
    char line[200];
    std::snprintf (line, sizeof line, "%s\n%d file%s, %d bars analysed\n\n", p.name.c_str(), p.files, p.files == 1 ? "" : "s", p.bars);
    s += line;
    s += "step         1   2   3   4   \n";
    for (int l = 0; l < kLanes; ++l)
    {
        if (! p.laneUsed (l)) continue;
        std::string row;
        for (int st = 0; st < 16; ++st)
        {
            row += shade[std::clamp ((int) std::lround (p.prob[(size_t) l][(size_t) st] * 8.0f), 0, 8)];
            if (st % 4 == 3) row += ' ';
        }
        std::snprintf (line, sizeof line, "%-11s  %s  vel %3.0f  %.1f/bar\n", laneName (l), row.c_str(), p.velMean[(size_t) l], p.hitsPerBar[(size_t) l]);
        s += line;
    }
    std::snprintf (line, sizeof line, "\nswing %.2f   off-grid %.0f%%   hits/bar %.1f (%.1f - %.1f)\ntempo %.0f BPM (%.0f - %.0f)\n",
                   p.swing, p.offGrid * 100.0f, p.densityMean, p.densityP05, p.densityP95, p.bpmMean, p.bpmP05, p.bpmP95);
    s += line;
    return s;
}

// ------------------------------------------------------------------ serialisation
namespace
{
    struct W
    {
        std::vector<uint8_t>& b;
        void u32 (uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) (v >> (8 * i))); }
        void f32 (float v) { uint32_t u; std::memcpy (&u, &v, 4); u32 (u); }
    };
    struct R
    {
        const uint8_t* d; size_t n, i = 0; bool bad = false;
        uint32_t u32() { if (i + 4 > n) { bad = true; return 0; } uint32_t v = 0; for (int k = 0; k < 4; ++k) v |= (uint32_t) d[i + (size_t) k] << (8 * k); i += 4; return v; }
        float f32 (float lo, float hi) { const uint32_t u = u32(); float v; std::memcpy (&v, &u, 4); if (! std::isfinite (v)) { bad = true; return lo; } return std::clamp (v, lo, hi); }
    };
}

std::vector<uint8_t> StyleProfile::toBytes() const
{
    std::vector<uint8_t> b { 'I', 'C', 'G', '1' };
    W w { b };
    w.u32 ((uint32_t) std::min<size_t> (name.size(), 200));
    b.insert (b.end(), name.begin(), name.begin() + (long) std::min<size_t> (name.size(), 200));
    w.u32 ((uint32_t) files); w.u32 ((uint32_t) bars);
    for (const auto& l : prob) for (float v : l) w.f32 (v);
    for (float v : velMean) w.f32 (v);
    for (float v : velStd) w.f32 (v);
    for (float v : hitsPerBar) w.f32 (v);
    for (const auto& l : pitchDist) for (float v : l) w.f32 (v);
    for (float v : { swing, offGrid, densityMean, densityP05, densityP95, bpmMean, bpmP05, bpmP95 }) w.f32 (v);
    return b;
}

bool StyleProfile::fromBytes (const uint8_t* d, size_t n)
{
    if (d == nullptr || n < 12 || std::memcmp (d, "ICG1", 4) != 0) return false;
    R r { d, n, 4 };
    StyleProfile p;
    const uint32_t nl = r.u32();
    if (nl > 200 || r.i + nl > n) return false;
    p.name.assign (reinterpret_cast<const char*> (d + r.i), nl);
    r.i += nl;
    p.files = (int) std::min<uint32_t> (r.u32(), 2000000000u);
    p.bars  = (int) std::min<uint32_t> (r.u32(), 2000000000u);
    for (auto& l : p.prob) for (float& v : l) v = r.f32 (0.0f, 1.0f);
    for (float& v : p.velMean) v = r.f32 (0.0f, 127.0f);
    for (float& v : p.velStd) v = r.f32 (0.0f, 127.0f);
    for (float& v : p.hitsPerBar) v = r.f32 (0.0f, 1000.0f);
    for (auto& l : p.pitchDist) for (float& v : l) v = r.f32 (0.0f, 1.0f);
    p.swing = r.f32 (0.5f, 0.75f); p.offGrid = r.f32 (0.0f, 1.0f);
    p.densityMean = r.f32 (0.0f, 10000.0f); p.densityP05 = r.f32 (0.0f, 10000.0f); p.densityP95 = r.f32 (0.0f, 10000.0f);
    p.bpmMean = r.f32 (0.0f, 1000.0f); p.bpmP05 = r.f32 (0.0f, 1000.0f); p.bpmP95 = r.f32 (0.0f, 1000.0f);
    if (r.bad || r.i != n) return false;
    *this = std::move (p);
    return true;
}

} // namespace icc
