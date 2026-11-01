#pragma once

// Milestone lines: how far the boot got (ROM -> runtime -> threads -> RSP ->
// display lists), printed to stderr.

#include <cstdarg>
#include <cstdio>

namespace ogre::milestones {

// Logs a single milestone line: `[tag] message`.
inline void log(const char* tag, const char* message) {
    char line[512];
    int n = snprintf(line, sizeof(line), "[%s] %s\n", tag, message);
    if (n < 0) {
        return;
    }
    fprintf(stderr, "%s", line);
}

}  // namespace ogre::milestones

// Logs `[tag] <printf-style args>`.
#define OGRE_MILESTONE(tag, ...)                                                      \
    do {                                                                              \
        char _ogre_milestone_msg[384];                                                \
        snprintf(_ogre_milestone_msg, sizeof(_ogre_milestone_msg), __VA_ARGS__);      \
        ::ogre::milestones::log((tag), _ogre_milestone_msg);                          \
    } while (0)

