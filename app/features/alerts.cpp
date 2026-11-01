#include "alerts.hpp"

#include <chrono>
#include <cstdio>
#include <deque>
#include <mutex>

namespace ogre::alerts {

namespace {

struct Stored {
    std::string text;
    Kind kind;
    std::chrono::steady_clock::time_point posted;
};

std::mutex g_mutex;
std::deque<Stored> g_alerts;
constexpr size_t MAX_KEPT = 16;

}  // namespace

void post(Kind kind, std::string text) {
    std::fprintf(stderr, "[alert] %s\n", text.c_str());
    std::lock_guard<std::mutex> lock(g_mutex);
    g_alerts.push_back({std::move(text), kind, std::chrono::steady_clock::now()});
    while (g_alerts.size() > MAX_KEPT) {
        g_alerts.pop_front();
    }
}

std::vector<Alert> recent(double lifetime) {
    const auto now = std::chrono::steady_clock::now();
    std::vector<Alert> out;
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const Stored& a : g_alerts) {
        const double age = std::chrono::duration<double>(now - a.posted).count();
        if (age < lifetime) {
            out.push_back({a.text, a.kind, age});
        }
    }
    return out;
}

}  // namespace ogre::alerts

