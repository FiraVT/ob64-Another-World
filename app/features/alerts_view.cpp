#include "alerts_view.hpp"

#include <RmlUi/Core.h>

#include <cstdio>
#include <string>

#include "alerts.hpp"

namespace ogre::alerts_view {

namespace {

constexpr double LIFETIME = 7.0;  // seconds on screen
constexpr double FADE = 1.0;      // the last second fades out

Rml::ElementDocument* g_document = nullptr;
Rml::Element* g_list = nullptr;
std::string g_shown;

std::string escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += c;
        }
    }
    return out;
}

}  // namespace

bool init(Rml::Context* context, const std::filesystem::path& asset_dir) {
    g_document = context->LoadDocument((asset_dir / "alerts.rml").string());
    if (g_document == nullptr) {
        std::fprintf(stderr, "[alerts] could not load alerts.rml\n");
        return false;
    }
    g_list = g_document->GetElementById("alerts");
    g_document->Show();
    return g_list != nullptr;
}

void update() {
    if (g_list == nullptr) {
        return;
    }
    std::string rml;
    for (const alerts::Alert& a : alerts::recent(LIFETIME)) {
        const char* kind = a.kind == alerts::Kind::Threat ? "threat" : a.kind == alerts::Kind::Good ? "good" : "info";
        // Opacity in tenths, so the markup only changes ten times during a fade.
        const double left = LIFETIME - a.age;
        const int tenths = left >= FADE ? 10 : static_cast<int>(left / FADE * 10.0);
        rml += "<div class=\"alert " + std::string(kind) + "\" style=\"opacity: " + std::to_string(tenths / 10.0) +
               "\">" + escape(a.text) + "</div>";
    }
    if (rml != g_shown) {
        g_shown = rml;
        g_list->SetInnerRML(rml);
    }
}

}  // namespace ogre::alerts_view

