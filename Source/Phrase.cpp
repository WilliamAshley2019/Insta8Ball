#include "Phrase.h"

#include <algorithm>
#include <cmath>

namespace icc
{

const char* phraseRoleName (PhraseRole r) noexcept
{
    switch (r)
    {
        case PhraseRole::Opening:      return "Opening";
        case PhraseRole::Continuation: return "Continuation";
        case PhraseRole::Development:  return "Development";
        case PhraseRole::Climax:       return "Climax";
        case PhraseRole::Cadence:      return "Cadence";
    }
    return "?";
}

std::vector<PhrasePlan> makePhrasePlan (int barsIn, Rng& rng)
{
    const int bars = std::clamp (barsIn, 1, kMaxBars);
    const int len  = bars >= 13 ? 4 : 2;

    std::vector<PhrasePlan> plan;
    for (int b = 0; b < bars; b += len)
    {
        PhrasePlan p;
        p.startBar   = b;
        p.lengthBars = std::min (len, bars - b);
        plan.push_back (p);
    }

    const int n = (int) plan.size();
    const int climaxAt = n >= 4 ? std::clamp ((int) std::lround ((double) (n - 1) * 0.66), 1, n - 2) : -1;

    for (int i = 0; i < n; ++i)
    {
        auto& p = plan[(size_t) i];
        if (i == n - 1)         p.role = n == 1 ? PhraseRole::Opening : PhraseRole::Cadence;
        else if (i == 0)        p.role = PhraseRole::Opening;
        else if (i == climaxAt) p.role = PhraseRole::Climax;
        else                    p.role = (i % 2 == 1) ? PhraseRole::Continuation : PhraseRole::Development;

        switch (p.role)
        {
            case PhraseRole::Opening:      p.targetPitch = 70; p.activity = 0.55f; p.tension = 0.30f; break;
            case PhraseRole::Continuation: p.targetPitch = 72; p.activity = 0.65f; p.tension = 0.45f; break;
            case PhraseRole::Development:  p.targetPitch = 74; p.activity = 0.75f; p.tension = 0.60f; break;
            case PhraseRole::Climax:       p.targetPitch = 78; p.activity = 0.92f; p.tension = 0.85f; break;
            case PhraseRole::Cadence:      p.targetPitch = 71; p.activity = 0.50f; p.tension = 0.20f; break;
        }
        p.targetPitch += rng.nextInt (3) - 1;
        p.shouldResolve = (i == n - 1);
        p.rideSection   = (p.role == PhraseRole::Climax) && rng.nextBool();
    }
    return plan;
}

const PhrasePlan& planAtBar (const std::vector<PhrasePlan>& plan, int bar) noexcept
{
    for (const auto& p : plan)
        if (bar >= p.startBar && bar < p.startBar + p.lengthBars) return p;
    return plan.back();
}

bool isFirstBarOfPhrase (const PhrasePlan& p, int bar) noexcept { return bar == p.startBar; }
bool isLastBarOfPhrase (const PhrasePlan& p, int bar) noexcept  { return bar == p.startBar + p.lengthBars - 1; }

} // namespace icc
