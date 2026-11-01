#include "game_events.hpp"

#include <vector>

namespace ogre::events {

namespace {

struct Subscriber {
    Event event;
    const char* owner;
    Handler handler;
};

// A function-local table, so subscribers registered by other translation units'
// static initialisers find it constructed.
std::vector<Subscriber>& table() {
    static std::vector<Subscriber> subscribers;
    return subscribers;
}

}  // namespace

void on(Event event, const char* owner, Handler handler) {
    table().push_back({event, owner, std::move(handler)});
}

}  // namespace ogre::events

extern "C" void ogre_event(uint8_t* rdram, recomp_context* ctx, uint32_t event, uint32_t a0, uint32_t a1) {
    const ogre::events::Args args{rdram, ctx, a0, a1};
    for (const auto& s : ogre::events::table()) {
        if (static_cast<uint32_t>(s.event) == event) {
            s.handler(args);
        }
    }
}

