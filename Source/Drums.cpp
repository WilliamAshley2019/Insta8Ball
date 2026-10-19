#include "Drums.h"
#include "Mapping.h"

#include <algorithm>
#include <cmath>

namespace icc
{

namespace
{
    constexpr int kStep = kBarTicks / 16;   // 240 ticks = one 16th

    enum class Role { Kick, Snare, Hat, Open, Perc };

    // Pattern characters: X accent, x normal, g ghost (probabilistic, more likely when complex),
    // 1-9 optional hit (probability rises with complexity), . rest.
    struct LaneDef { int key; Role role; const char* a; const char* b; int vel; };

    struct StyleDef
    {
        const char* name; int bpm; float swing; BassMode bass; float sustain; float velScale;
        bool fourFloor, rolls, ghosts, tomFill;
        std::vector<LaneDef> lanes;
    };

    const std::vector<StyleDef>& styleTable()
    {
        using namespace gm;
        static const std::vector<StyleDef> t = {
            { "Pop Rock", 110, 0.50f, BassMode::Eighths, 0.7f, 1.0f, false, false, false, true, {
                { kick,  Role::Kick,  "X.......x.x.....", "X.....x.x.x.....", 100 },
                { snare, Role::Snare, "....X.......X...", "",                 94 },
                { hatClosed, Role::Hat, "x.x.x.x.x.x.x.x.", "",               70 } } },
            { "Four On The Floor", 124, 0.50f, BassMode::Offbeat, 0.5f, 1.0f, true, false, false, false, {
                { kick,  Role::Kick,  "X...X...X...X...", "",                 104 },
                { clap,  Role::Snare, "....X.......X...", "",                  96 },
                { hatClosed, Role::Hat, "x.x.x.x.x.x.x.x.", "",               66 },
                { hatOpen, Role::Open, "..x...x...x...x.", "",                 78 },
                { shaker, Role::Perc, "5656565656565656", "",                  52 } } },
            { "Boom Bap", 90, 0.58f, BassMode::KickLock, 0.5f, 1.0f, false, false, true, false, {
                { kick,  Role::Kick,  "X.....x...x.....", "X.....x..x.x....",  104 },
                { snare, Role::Snare, "....X..g....X..g", "",                 100 },
                { hatClosed, Role::Hat, "x.x.x.x.x.x.x.x.", "",               64 } } },
            { "Trap", 140, 0.50f, BassMode::KickLock, 1.0f, 1.0f, false, true, false, false, {
                { kick,  Role::Kick,  "X.....x....x.x..", "X..x.....x..x.x.",  106 },
                { snare, Role::Snare, "........X.......", "",                 100 },
                { clap,  Role::Snare, "........X.......", "",                  84 },
                { hatClosed, Role::Hat, "x.x.x.x.x.x.x.x.", "",               66 },
                { hatOpen, Role::Open, "..............4.", "",                 72 } } },
            { "Lo-Fi", 80, 0.63f, BassMode::KickLock, 0.6f, 0.9f, false, false, true, false, {
                { kick,  Role::Kick,  "X.....x..x......", "X.......x.x.....",  100 },
                { snare, Role::Snare, "....X.......X...", "",                  92 },
                { rim,   Role::Perc,  "......g.......g.", "",                  60 },
                { hatClosed, Role::Hat, "x.g.x.g.x.g.x.g.", "",                58 } } },
            { "Half-Time", 140, 0.50f, BassMode::KickLock, 0.8f, 1.0f, false, false, false, true, {
                { kick,  Role::Kick,  "X.........x.....", "X.......x.x.....",  104 },
                { snare, Role::Snare, "........X.......", "",                 100 },
                { hatClosed, Role::Hat, "x.x.x.x.x.x.x.x.", "",                68 } } },
            { "Funk", 100, 0.54f, BassMode::KickLock, 0.35f, 1.0f, false, false, true, false, {
                { kick,  Role::Kick,  "X.....x..x.x....", "X..x..x...x..x..",  102 },
                { snare, Role::Snare, "....X..g.g..X..g", "",                  98 },
                { hatClosed, Role::Hat, "xxxxxxxxxxxxxxxx", "",                 66 },
                { hatOpen, Role::Open, "......x.......x.", "",                  76 } } },
            { "Breakbeat", 135, 0.52f, BassMode::KickLock, 0.4f, 1.0f, false, false, true, true, {
                { kick,  Role::Kick,  "X.x.....x.x.....", "X.x......xx.....",  104 },
                { snare, Role::Snare, "....X..g.g..X..g", "",                  98 },
                { hatClosed, Role::Hat, "x.x.x.x.x.x.x.x.", "",                 66 },
                { hatOpen, Role::Open, "...............x", "",                  74 } } },
            { "Drum & Bass", 170, 0.50f, BassMode::KickLock, 0.4f, 1.0f, false, false, true, false, {
                { kick,  Role::Kick,  "X.........x.....", "X.........x.x...",  106 },
                { snare, Role::Snare, "..g.X..g..g.X..g", "",                 100 },
                { hatClosed, Role::Hat, "x.x.x.x.x.x.x.x.", "",                 62 },
                { hatOpen, Role::Open, "..x...x...x...x.", "",                  70 } } },
            { "Reggae One Drop", 75, 0.60f, BassMode::KickLock, 0.9f, 0.9f, false, false, false, false, {
                { kick,  Role::Kick,  "........X.......", "",                  98 },
                { rim,   Role::Snare, "........X.......", "",                  92 },
                { hatClosed, Role::Hat, "x.x.x.x.x.x.x.x.", "",                 58 },
                { shaker, Role::Perc, "5555555555555555", "",                  44 } } },
            { "Disco", 118, 0.50f, BassMode::Pulse, 0.6f, 1.0f, true, false, false, true, {
                { kick,  Role::Kick,  "X...X...X...X...", "",                 102 },
                { snare, Role::Snare, "....X.......X...", "",                  96 },
                { hatClosed, Role::Hat, "x.x.x.x.x.x.x.x.", "",                 66 },
                { hatOpen, Role::Open, "..x...x...x...x.", "",                  82 } } },
            { "Bossa Nova", 120, 0.50f, BassMode::KickLock, 0.8f, 0.78f, false, false, false, false, {
                { kick,  Role::Kick,  "X.....X.X.....X.", "",                  90 },
                { rim,   Role::Snare, "X..x..x...x.x...", "",                  80 },
                { hatClosed, Role::Hat, "x.x.x.x.x.x.x.x.", "",                 54 } } },
            { "Shuffle", 100, 0.67f, BassMode::Eighths, 0.7f, 1.0f, false, false, false, true, {
                { kick,  Role::Kick,  "X.......x.......", "X.......x.x.....",  100 },
                { snare, Role::Snare, "....X.......X...", "",                  96 },
                { hatClosed, Role::Hat, "x.x.x.x.x.x.x.x.", "",                 68 } } },
            { "Techno", 132, 0.50f, BassMode::Offbeat, 0.45f, 1.0f, true, false, false, false, {
                { kick,  Role::Kick,  "X...X...X...X...", "",                 108 },
                { clap,  Role::Snare, "....X.......X...", "",                  90 },
                { hatClosed, Role::Hat, "xxxxxxxxxxxxxxxx", "",                 50 },
                { hatOpen, Role::Open, "..x...x...x...x.", "",                  80 },
                { rim,   Role::Perc,  "..5..5.5..5.5..5", "",                  60 } } },
            { "Afro-Latin", 105, 0.56f, BassMode::KickLock, 0.4f, 0.92f, false, false, false, false, {
                { kick,  Role::Kick,  "X..x..x...x.x...", "",                  98 },
                { rim,   Role::Snare, "....x.....x..x..", "",                  84 },
                { cowbell, Role::Perc, "x..x..x.x..x..x.", "",                 66 },
                { congaHi, Role::Perc, "..x.x...x.x.x...", "",                 70 },
                { congaLo, Role::Perc, "....x.......x...", "",                 74 },
                { shaker, Role::Perc, "5555555555555555", "",                  46 } } },
            { "Synthwave", 100, 0.50f, BassMode::Pulse, 0.85f, 1.0f, false, false, false, true, {
                { kick,  Role::Kick,  "X.......X..x....", "X.......X.x.....",  104 },
                { snare, Role::Snare, "....X.......X...", "",                 100 },
                { clap,  Role::Snare, "....X.......X...", "",                  80 },
                { hatClosed, Role::Hat, "x.x.x.x.x.x.x.x.", "",                 64 } } },
        };
        return t;
    }

    struct Ev { int tick; int key; int vel; int len; Role role; bool anchor; };

    int stepTick (int bar, int step, float ratio) noexcept
    {
        int t = bar * kBarTicks + (step & ~1) * kStep;
        if (step & 1) t += (int) std::lround (2.0f * (float) kStep * ratio);
        return t;
    }

    int jitterTicks (Role r) noexcept
    {
        switch (r) { case Role::Kick: return 8; case Role::Snare: return 12; case Role::Hat: return 16;
                     case Role::Open: return 12; default: return 14; }
    }

    // One fill = list of {step offset within the fill, key, velocity}.
    struct FillHit { int step; int key; int vel; };

    std::vector<FillHit> buildFill (int type, int len, bool useClap, bool tomHeavy, Rng& rng)
    {
        std::vector<FillHit> f;
        const int s0 = 16 - len;
        auto ramp = [len] (int i, int lo, int hi) { return lo + (hi - lo) * i / std::max (1, len - 1); };
        static const int toms[] = { gm::tomHi, gm::tomHiMid, gm::tomLoMid, gm::tomLo, gm::tomFloorHi, gm::tomFloorLo };

        if (tomHeavy && type == 0) type = 1;
        switch (type)
        {
            case 0:   // snare roll, crescendo
                for (int i = 0; i < len; ++i) f.push_back ({ s0 + i, gm::snare, ramp (i, 62, 112) });
                break;
            case 1:   // snare then toms falling
                f.push_back ({ s0, gm::snare, 104 });
                for (int i = 1; i < len; ++i) f.push_back ({ s0 + i, toms[std::min (5, i * 6 / len)], ramp (i, 96, 110) });
                break;
            case 2:   // syncopated snare / tom answer
                for (int i = 0; i < len; ++i)
                {
                    const bool hit = len == 4 ? (i == 0 || i == 2 || i == 3) : (i == 0 || i == 2 || i == 3 || i == 5 || i == 6 || i == 7);
                    if (hit) f.push_back ({ s0 + i, (i % 3 == 2) ? toms[rng.nextInt (3)] : gm::snare, ramp (i, 88, 112) });
                }
                break;
            default:  // build on 8ths with clap or snare
                for (int i = 0; i < len; i += 2) f.push_back ({ s0 + i, useClap ? gm::clap : gm::snare, ramp (i, 76, 110) });
                f.push_back ({ 15, gm::snare, 112 });
                break;
        }
        return f;
    }

    int noteLength (int key, Role role, bool ride)
    {
        if (ride) return 240;
        if (key == gm::crash) return kBarTicks / 2;
        if (role == Role::Kick) return 200;
        if (role == Role::Open) return 300;
        if (role == Role::Hat) return 90;
        if (role == Role::Snare) return 150;
        return 130;
    }
}

int drumStyleCount() noexcept { return (int) styleTable().size(); }
const char* drumStyleName (int i) noexcept { return styleTable()[(size_t) std::clamp (i, 0, drumStyleCount() - 1)].name; }
int drumStyleBpm (int i) noexcept { return styleTable()[(size_t) std::clamp (i, 0, drumStyleCount() - 1)].bpm; }

DrumResult generateDrums (const DrumSettings& s, const std::vector<PhrasePlan>& plan, Rng& rng)
{
    const auto& styles = styleTable();
    const int   bars   = std::clamp (s.bars, 1, kMaxBars);
    const int   idx    = s.style >= 0 ? std::min (s.style, drumStyleCount() - 1) : rng.nextInt (drumStyleCount());
    const StyleDef& st = styles[(size_t) idx];

    const float cx = std::clamp (s.complexity, 0.0f, 1.0f), hum = std::clamp (s.humanize, 0.0f, 1.0f),
                fl = std::clamp (s.fills, 0.0f, 1.0f);
    const float ratio = std::clamp (std::max (st.swing, 0.5f + std::clamp (s.swing, 0.0f, 1.0f) * 0.25f), 0.5f, 0.75f);

    DrumResult r;
    r.style = idx; r.bassMode = st.bass; r.sustain = st.sustain;
    r.anchors.assign ((size_t) bars, {});
    r.kicks.assign ((size_t) bars, {});

    bool hasClap = false;
    for (const auto& l : st.lanes) hasClap = hasClap || l.key == gm::clap;

    // ---- phrase level decisions: fills and crashes ----
    struct FillPlan { bool on = false; int type = 0, len = 4; };
    std::vector<FillPlan> fills ((size_t) bars);
    std::vector<char>     crash ((size_t) bars, 0);

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto& pp = planAtBar (plan, bar);
        const bool phraseEnd = isLastBarOfPhrase (pp, bar) || bar == bars - 1;
        if (phraseEnd && fl > 0.01f)
        {
            float p = fl * (pp.role == PhraseRole::Cadence ? 1.0f : pp.role == PhraseRole::Climax ? 0.85f : 0.55f);
            if (bar == bars - 1) p = std::max (p, fl);
            if (rng.nextFloat() < p)
            {
                auto& f = fills[(size_t) bar];
                f.on   = true;
                f.len  = (cx > 0.4f && rng.nextFloat() < 0.5f) ? 8 : 4;
                f.type = rng.nextInt (4);
            }
        }
    }
    for (int bar = 0; bar < bars; ++bar)
    {
        const auto& pp = planAtBar (plan, bar);
        float p = 0.0f;
        if (bar == 0)                         p = 0.5f + 0.4f * fl;
        else if (isFirstBarOfPhrase (pp, bar)) p = pp.role == PhraseRole::Climax ? 1.0f : 0.2f + 0.5f * fl;
        if (bar > 0 && fills[(size_t) bar - 1].on) p = std::max (p, 0.9f);
        if (bar == 0 && fills[(size_t) bars - 1].on) p = std::max (p, 0.95f);   // the loop wraps after the fill
        crash[(size_t) bar] = rng.nextFloat() < p ? 1 : 0;
    }

    // ---- bars ----
    std::vector<Ev> all;
    for (int bar = 0; bar < bars; ++bar)
    {
        const auto& pp = planAtBar (plan, bar);
        const float activity = pp.activity;
        const bool  alt = (bar & 1) != 0;
        std::vector<Ev> ev;
        bool stepUsed[16] = {};   // any kick step taken (for extra-kick placement)

        for (const auto& lane : st.lanes)
        {
            const char* pat = (alt && lane.b[0] != '\0') ? lane.b : lane.a;
            for (int step = 0; step < 16; ++step)
            {
                const char c = pat[step];
                if (c == '.') continue;

                float prob = 1.0f;
                int   vel  = lane.vel;
                bool  anchor = lane.role == Role::Kick || lane.role == Role::Snare;

                if (c == 'X') vel += 16;
                else if (c == 'g') { prob = 0.25f + 0.7f * cx * (st.ghosts ? 1.0f : 0.7f); vel = 34; anchor = false; }
                else if (c >= '1' && c <= '9') { prob = std::min (1.0f, (float) (c - '0') / 9.0f * (0.25f + 1.25f * cx)); anchor = false; }

                if (lane.role == Role::Hat)
                {
                    if (c == 'x') vel = lane.vel + (step % 4 == 0 ? 12 : (step % 2 == 0 ? 0 : -14));
                    if ((step & 1) && activity < 0.65f) prob *= std::max (0.3f, 1.0f - (0.65f - activity) * 2.0f);
                }
                if (prob < 1.0f && rng.nextFloat() >= prob) continue;

                int key = lane.key, len = noteLength (lane.key, lane.role, false);
                if (lane.role == Role::Hat && pp.rideSection) { key = gm::ride; len = noteLength (key, lane.role, true); vel += 6; }
                else if (lane.role == Role::Open && pp.rideSection) continue;

                vel = (int) std::lround ((float) vel * st.velScale * (0.9f + 0.2f * activity));
                ev.push_back ({ stepTick (bar, step, ratio), key, vel + rng.nextInt (7), len, lane.role, anchor });
                if (lane.role == Role::Kick) stepUsed[step] = true;
            }
        }

        // an open hat chokes the closed hat on the same step
        std::vector<int> openTicks;
        for (const auto& o : ev)
            if (o.role == Role::Open) openTicks.push_back (o.tick);
        if (! openTicks.empty())
            ev.erase (std::remove_if (ev.begin(), ev.end(), [&openTicks] (const Ev& e)
                                      { return e.role == Role::Hat && std::find (openTicks.begin(), openTicks.end(), e.tick) != openTicks.end(); }),
                      ev.end());

        // complexity: an extra kick, a hat pickup
        if (! st.fourFloor && rng.nextFloat() < cx * 0.35f)
        {
            static const int cand[] = { 6, 7, 10, 11, 13, 14, 15 };
            const int step = cand[rng.nextInt (7)];
            if (! stepUsed[step]) ev.push_back ({ stepTick (bar, step, ratio), gm::kick, 82 + rng.nextInt (10), noteLength (gm::kick, Role::Kick, false), Role::Kick, true });
        }
        if (! st.fourFloor && rng.nextFloat() < cx * 0.4f)
            ev.push_back ({ stepTick (bar, 15, ratio), gm::hatClosed, 44 + rng.nextInt (10), 80, Role::Hat, false });

        // trap-style hat rolls
        if (st.rolls && rng.nextFloat() < 0.25f + 0.55f * cx)
        {
            static const int at[] = { 7, 11, 14, 15 };
            const int step = at[rng.nextInt (4)];
            const int startTick = stepTick (bar, step, ratio);
            ev.erase (std::remove_if (ev.begin(), ev.end(), [startTick] (const Ev& e) { return e.role == Role::Hat && e.tick >= startTick && e.tick < startTick + kStep; }), ev.end());
            const int kind = rng.nextInt (3), div = kind == 0 ? 2 : (kind == 1 ? 4 : 3), gap = kStep / div;
            for (int i = 0; i < div; ++i)
                ev.push_back ({ startTick + i * gap, gm::hatClosed, 52 + 40 * i / div + rng.nextInt (6), std::max (30, gap - 8), Role::Hat, false });
        }

        // fill
        const auto& fp = fills[(size_t) bar];
        if (fp.on)
        {
            const int fillTick = stepTick (bar, 16 - fp.len, ratio);
            ev.erase (std::remove_if (ev.begin(), ev.end(), [fillTick] (const Ev& e) { return e.tick >= fillTick && e.role != Role::Kick; }), ev.end());
            for (const auto& h : buildFill (fp.type, fp.len, hasClap, st.tomFill, rng))
            {
                const Role role = (h.key == gm::snare || h.key == gm::clap) ? Role::Snare : Role::Perc;
                ev.push_back ({ stepTick (bar, h.step, ratio), h.key, h.vel, noteLength (h.key, role, false), role, false });
            }
        }
        if (crash[(size_t) bar])
            ev.push_back ({ bar * kBarTicks, gm::crash, 100 + rng.nextInt (16), noteLength (gm::crash, Role::Perc, false), Role::Perc, false });

        for (const auto& e : ev)
        {
            if (e.anchor && e.vel >= 60) r.anchors[(size_t) bar].push_back (e.tick);
            if (e.role == Role::Kick) r.kicks[(size_t) bar].push_back (e.tick);
            all.push_back (e);
        }

        // melody anchors: keep a workable handful
        auto& an = r.anchors[(size_t) bar];
        an.push_back (bar * kBarTicks);
        std::sort (an.begin(), an.end());
        an.erase (std::unique (an.begin(), an.end()), an.end());
        if (an.size() < 3) for (int b : { 1, 2, 3 }) an.push_back (bar * kBarTicks + b * kPPQ);
        std::sort (an.begin(), an.end());
        an.erase (std::unique (an.begin(), an.end()), an.end());
        constexpr size_t maxAnchors = 8;
        if (an.size() > maxAnchors)
        {
            std::vector<int> reduced;
            for (size_t i = 0; i < maxAnchors; ++i) reduced.push_back (an[i * (an.size() - 1) / (maxAnchors - 1)]);
            reduced.erase (std::unique (reduced.begin(), reduced.end()), reduced.end());
            an = std::move (reduced);
        }
        auto& kk = r.kicks[(size_t) bar];
        std::sort (kk.begin(), kk.end());
        kk.erase (std::unique (kk.begin(), kk.end()), kk.end());
    }

    // ---- humanise + emit ----
    for (const auto& e : all)
    {
        int tick = e.tick, vel = e.vel;
        if (hum > 0.0f)
        {
            const int bar = e.tick / kBarTicks;
            const int j = (int) std::lround ((rng.nextFloat() * 2.0f - 1.0f) * hum * (float) jitterTicks (e.role));
            tick = std::clamp (e.tick + j, bar * kBarTicks, bar * kBarTicks + kBarTicks - 1);
            vel += (int) std::lround ((rng.nextFloat() * 2.0f - 1.0f) * hum * 10.0f);
        }
        r.notes.push_back ({ tick, e.len, e.key, std::clamp (vel, 1, 127) });
    }
    std::stable_sort (r.notes.begin(), r.notes.end(), [] (const Note& a, const Note& b)
                      { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
    // drop exact duplicates (same key, same tick)
    r.notes.erase (std::unique (r.notes.begin(), r.notes.end(), [] (const Note& a, const Note& b) { return a.start == b.start && a.pitch == b.pitch; }),
                   r.notes.end());
    return r;
}

DrumResult analyseDrums (const std::vector<Note>& notes, int barsIn)
{
    const int bars = std::clamp (barsIn, 1, kMaxBars);
    DrumResult r;
    r.anchors.assign ((size_t) bars, {});
    r.kicks.assign ((size_t) bars, {});
    r.bassMode = BassMode::KickLock;
    for (const auto& n : notes)
    {
        const int bar = n.start / kBarTicks;
        if (n.start < 0 || bar >= bars) continue;
        const bool kick = n.pitch == 35 || n.pitch == gm::kick;
        const bool snare = n.pitch >= gm::rim && n.pitch <= 40;
        if (kick)  r.kicks[(size_t) bar].push_back (n.start);
        if ((kick || snare) && n.velocity >= 60) r.anchors[(size_t) bar].push_back (n.start);
    }
    for (int bar = 0; bar < bars; ++bar)
    {
        auto& an = r.anchors[(size_t) bar];
        an.push_back (bar * kBarTicks);
        if (an.size() < 3) for (int b : { 1, 2, 3 }) an.push_back (bar * kBarTicks + b * kPPQ);
        std::sort (an.begin(), an.end());
        an.erase (std::unique (an.begin(), an.end()), an.end());
        if (an.size() > 8) an.resize (8);
        auto& kk = r.kicks[(size_t) bar];
        std::sort (kk.begin(), kk.end());
        kk.erase (std::unique (kk.begin(), kk.end()), kk.end());
    }
    return r;
}

} // namespace icc
