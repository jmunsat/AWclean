/*
 * AWclean - plain-English overlay on top of Airwindows Consolidated
 *
 * Looks up the overlay for an Airwindows plugin and turns a 0..1 control value
 * into a readout with real units, using the formulas in awclean/overlay.json.
 *
 * This source released under the MIT License, found in ~/LICENSE.md.
 */

#ifndef AWCLEAN_AWCLEAN_H
#define AWCLEAN_AWCLEAN_H

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "AWCleanOverlay.h"

namespace awclean
{
inline const Entry *lookup(const std::string &name)
{
    static const std::unordered_map<std::string, const Entry *> byName = []() {
        std::unordered_map<std::string, const Entry *> m;
        for (int i = 0; i < numEntries; ++i)
            m[entries[i].name] = &entries[i];
        return m;
    }();
    auto f = byName.find(name);
    return f == byName.end() ? nullptr : f->second;
}

inline const Param *param(const Entry *e, int i)
{
    if (!e || i < 0 || i >= e->nParams || !e->params)
        return nullptr;
    return &e->params[i];
}

// A tiny recursive descent evaluator for the overlay formulas.
// Supports + - * / ^, parentheses, unary minus, numbers, x, sr,
// and log10 abs floor sqrt.
struct Expr
{
    const char *s;
    double x, sr;
    bool ok{true};

    void ws()
    {
        while (*s && std::isspace((unsigned char)*s))
            s++;
    }

    double primary()
    {
        ws();
        if (*s == '(')
        {
            s++;
            auto v = sum();
            ws();
            if (*s == ')')
                s++;
            else
                ok = false;
            return v;
        }
        if (std::isdigit((unsigned char)*s) || *s == '.')
        {
            char *end;
            auto v = std::strtod(s, &end);
            s = end;
            return v;
        }
        if (std::isalpha((unsigned char)*s))
        {
            std::string id;
            while (std::isalnum((unsigned char)*s) || *s == '_')
                id += *s++;
            if (id == "x")
                return x;
            if (id == "sr")
                return sr;
            auto v = primary();
            if (id == "log10")
                return std::log10(v);
            if (id == "abs")
                return std::fabs(v);
            if (id == "floor")
                return std::floor(v);
            if (id == "sqrt")
                return std::sqrt(v);
            ok = false;
            return 0;
        }
        ok = false;
        return 0;
    }

    double unary()
    {
        ws();
        if (*s == '-')
        {
            s++;
            return -unary();
        }
        if (*s == '+')
        {
            s++;
            return unary();
        }
        return power();
    }

    double power()
    {
        auto b = primary();
        ws();
        if (*s == '^')
        {
            s++;
            return std::pow(b, unary());
        }
        return b;
    }

    double product()
    {
        auto v = unary();
        for (;;)
        {
            ws();
            if (*s == '*')
            {
                s++;
                v *= unary();
            }
            else if (*s == '/')
            {
                s++;
                v /= unary();
            }
            else
                return v;
        }
    }

    double sum()
    {
        auto v = product();
        for (;;)
        {
            ws();
            if (*s == '+')
            {
                s++;
                v += product();
            }
            else if (*s == '-')
            {
                s++;
                v -= product();
            }
            else
                return v;
        }
    }

    static bool eval(const char *f, double x, double sr, double &out)
    {
        Expr e{f, x, sr};
        out = e.sum();
        e.ws();
        return e.ok && *e.s == 0 && std::isfinite(out);
    }
};

inline std::vector<std::string> steps(const Param *p)
{
    std::vector<std::string> res;
    if (!p || !p->steps)
        return res;
    std::string cur;
    for (const char *c = p->steps; *c; ++c)
    {
        if (*c == '|')
        {
            res.push_back(cur);
            cur.clear();
        }
        else
            cur += *c;
    }
    res.push_back(cur);
    return res;
}

inline int stepIndex(float x, int n)
{
    int i = (int)std::floor(x * n);
    return i < 0 ? 0 : (i >= n ? n - 1 : i);
}

// Returns true and fills out when the overlay has its own readout for this control.
inline bool display(const Param *p, float x, double sr, std::string &out)
{
    if (!p)
        return false;
    if (p->steps)
    {
        auto st = steps(p);
        out = st[stepIndex(x, (int)st.size())];
        return true;
    }
    if (!p->formula)
        return false;

    double v;
    if (!Expr::eval(p->formula, x, sr, v))
        return false;

    std::string unit = p->unit ? p->unit : "";
    if (unit == "dB" && v < -96)
    {
        out = "-inf dB";
        return true;
    }

    int d = p->decimals;
    if (d < 0)
    {
        auto a = std::fabs(v);
        d = a >= 100 ? 0 : (a >= 10 ? 1 : 2);
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", d, v);
    out = buf;
    if (out == "-0" || out == "-0.0" || out == "-0.00")
        out = out.substr(1);
    if (!unit.empty())
        out += (unit == "%" || unit == "\302\260" ? "" : " ") + unit;
    return true;
}

// Inverse of display for typed-in values. Formulas are not all invertible in
// closed form, so search the knob range for the closest readout.
inline bool fromText(const Param *p, const std::string &text, double sr, float &out)
{
    if (!p)
        return false;
    if (p->steps)
    {
        auto st = steps(p);
        auto lower = [](std::string s) {
            for (auto &c : s)
                c = (char)std::tolower((unsigned char)c);
            return s;
        };
        auto t = lower(text);
        for (size_t i = 0; i < st.size(); ++i)
        {
            if (lower(st[i]).rfind(t, 0) == 0 && !t.empty())
            {
                out = (i + 0.5f) / st.size();
                return true;
            }
        }
        return false;
    }
    if (!p->formula)
        return false;

    char *end;
    auto target = std::strtod(text.c_str(), &end);
    if (end == text.c_str())
    {
        if (text.find("inf") != std::string::npos)
            target = -1000;
        else
            return false;
    }
    if (!std::isfinite(target)) // strtod reads "-inf" itself
        target = target < 0 ? -1000 : 1e9;

    static constexpr int N = 4000;
    double best{1e30};
    bool found{false};
    for (int i = 0; i <= N; ++i)
    {
        double xi = (double)i / N, v;
        if (!Expr::eval(p->formula, xi, sr, v))
            continue;
        auto dist = std::fabs(v - target);
        if (dist < best)
        {
            best = dist;
            out = (float)xi;
            found = true;
        }
    }
    return found;
}

inline std::string displayName(const std::string &name, const std::string &title)
{
    return title.empty() ? name : name + " - " + title;
}
} // namespace awclean
#endif
