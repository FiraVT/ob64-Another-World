// Port features switched from the Esc menu's GAMEPLAY tab, for sources that
// both builds compile. The desktop build reads the saved setting
// (settings.hpp -> feature_enabled / choice_on); the browser build has no
// settings.cpp, so there only the environment variable decides.
#pragma once

#include <cstdlib>

#include "options.hpp"

namespace ogre::feature {

// A feature that is ON unless switched off: the GAMEPLAY row's default.
inline bool enabled(Choice choice, const char* env) {
    return feature_enabled(choice, env);
}
#define OGRE_FEATURE(choice) ::ogre::Choice::choice

// The SKIP INTRO setting (off by default). Always off in the browser build.
inline bool skip_intro() {
    return choice_on(Choice::SkipIntro);
}

}  // namespace ogre::feature

