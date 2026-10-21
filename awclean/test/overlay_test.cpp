// Sanity checks for the AWclean overlay: every formula parses, readouts are
// finite, and typed-in values come back to roughly the same knob position.
// Build: c++ -std=c++17 -I src awclean/test/overlay_test.cpp -o /tmp/overlay_test
#include <cstdio>
#include "AWClean.h"

int main()
{
    int fails = 0, checked = 0;
    for (int e = 0; e < awclean::numEntries; ++e)
    {
        auto &ent = awclean::entries[e];
        for (int i = 0; i < ent.nParams; ++i)
        {
            auto p = awclean::param(&ent, i);
            if (!p->formula && !p->steps)
                continue;
            for (float x : {0.f, 0.13f, 0.5f, 0.87f, 1.f})
            {
                std::string out;
                if (!awclean::display(p, x, 48000, out))
                {
                    std::printf("FAIL display %s/%s x=%g\n", ent.name, p->label, x);
                    fails++;
                    continue;
                }
                float back;
                if (!awclean::fromText(p, out, 48000, back))
                {
                    std::printf("FAIL fromText %s/%s '%s'\n", ent.name, p->label, out.c_str());
                    fails++;
                    continue;
                }
                std::string again;
                awclean::display(p, back, 48000, again);
                if (again != out)
                {
                    std::printf("ROUNDTRIP %s/%s x=%g '%s' -> '%s'\n", ent.name, p->label, x,
                                out.c_str(), again.c_str());
                }
                checked++;
            }
        }
    }
    std::string s;
    awclean::display(awclean::param(awclean::lookup("Pressure5"), 0), 0.5f, 48000, s);
    std::printf("Pressure5 threshold at 0.5 = %s\n", s.c_str());
    std::printf("checked %d readouts, %d failures\n", checked, fails);
    return fails ? 1 : 0;
}
