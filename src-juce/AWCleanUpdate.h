/*
 * AWclean - checks GitHub releases for a newer AWclean build
 *
 * This source released under the MIT License, found in ~/LICENSE.md.
 */

#ifndef AWCLEAN_UPDATE_H
#define AWCLEAN_UPDATE_H

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "juce_core/juce_core.h"

namespace awclean
{
struct UpdateInfo
{
    enum State
    {
        NOT_CHECKED,
        CHECKING,
        UP_TO_DATE,
        AVAILABLE,
        FAILED
    } state{NOT_CHECKED};
    std::string latestVersion, pageUrl;
};

// Shared by every instance in the host, so a session with many instances asks GitHub once.
struct UpdateStatus
{
    static std::mutex &mutex()
    {
        static std::mutex m;
        return m;
    }
    static UpdateInfo &info()
    {
        static UpdateInfo i;
        return i;
    }
    static UpdateInfo get()
    {
        std::lock_guard<std::mutex> g(mutex());
        return info();
    }
    // bumps whenever the state changes so editors know to repaint
    static std::atomic<int> &generation()
    {
        static std::atomic<int> g{0};
        return g;
    }
};

inline std::vector<int> versionParts(const std::string &v)
{
    std::vector<int> res;
    auto s = juce::String(v).trimCharactersAtStart("vV");
    for (auto &p : juce::StringArray::fromTokens(s, ".", ""))
        res.push_back(p.getIntValue());
    return res;
}

inline bool isNewer(const std::string &latest, const std::string &current)
{
    auto a = versionParts(latest), b = versionParts(current);
    a.resize(std::max(a.size(), b.size()), 0);
    b.resize(a.size(), 0);
    return a > b;
}

struct UpdateChecker : juce::Thread
{
    std::string currentVersion, repo;
    UpdateChecker(const std::string &current, const std::string &repo)
        : juce::Thread("AWclean update check"), currentVersion(current), repo(repo)
    {
    }
    ~UpdateChecker() override { stopThread(8000); }

    // Starts a check unless one already ran this session. force re-checks.
    void check(bool force)
    {
        {
            std::lock_guard<std::mutex> g(UpdateStatus::mutex());
            auto &i = UpdateStatus::info();
            if (i.state == UpdateInfo::CHECKING)
                return;
            if (!force && i.state != UpdateInfo::NOT_CHECKED)
                return;
            i.state = UpdateInfo::CHECKING;
        }
        UpdateStatus::generation()++;
        if (isThreadRunning())
            stopThread(8000);
        startThread();
    }

    void run() override
    {
        UpdateInfo res;
        res.state = UpdateInfo::FAILED;

        auto url = juce::URL("https://api.github.com/repos/" + repo + "/releases/latest");
        auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                        .withExtraHeaders("User-Agent: AWclean\r\nAccept: application/vnd.github+json")
                        .withConnectionTimeoutMs(6000);
        if (auto stream = url.createInputStream(opts))
        {
            auto body = stream->readEntireStreamAsString();
            auto json = juce::JSON::parse(body);
            auto tag = json.getProperty("tag_name", "").toString().toStdString();
            if (!tag.empty())
            {
                res.latestVersion = juce::String(tag).trimCharactersAtStart("vV").toStdString();
                res.pageUrl = json.getProperty("html_url", "").toString().toStdString();
                if (res.pageUrl.empty())
                    res.pageUrl = "https://github.com/" + repo + "/releases/latest";
                res.state = isNewer(tag, currentVersion) ? UpdateInfo::AVAILABLE
                                                         : UpdateInfo::UP_TO_DATE;
            }
        }

        if (threadShouldExit())
            res.state = UpdateInfo::NOT_CHECKED;
        {
            std::lock_guard<std::mutex> g(UpdateStatus::mutex());
            UpdateStatus::info() = res;
        }
        UpdateStatus::generation()++;
    }
};
} // namespace awclean
#endif
