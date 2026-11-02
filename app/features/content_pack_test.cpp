// The content pack hostile-file test. app/CMakeLists.txt builds it and runs it
// after every build; a failure fails the build. A pack is data and runs no
// code, so the risk of a bad pack is a crash or a write outside the game's
// tables; each case is a file built to cause one, and must be refused with an
// error. docs/anotherworld/security.md lists them.

#include "content_db.hpp"
#include "content_files.hpp"
#include "recruit.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

// The test runs without a ROM: the database stays unloaded, which is how a
// pack is parsed before the ROM's records exist.
namespace recomp {
std::span<const uint8_t> get_rom() {
    return {};
}
namespace overlays {
bool is_function_bank_loaded(uint32_t) {
    return false;
}
}  // namespace overlays
}  // namespace recomp
namespace ogre::host {
std::filesystem::path exe_dir() {
    return std::filesystem::temp_directory_path() / "ogre-content-pack-test";
}
std::filesystem::path pref_dir() {
    return exe_dir();
}
}  // namespace ogre::host

namespace {

using namespace ogre::content;

struct Case {
    const char* name;
    std::string text;
    bool accept;
};

std::string nested(int depth) {
    return std::string(static_cast<size_t>(depth), '[') + std::string(static_cast<size_t>(depth), ']');
}

std::vector<Case> cases() {
    return {
        {"a plain pack is read", R"({"classes":{"34":{"str_base":92}},"items":{"1":{"name":"Iron Fang"}}})", true},
        {"a battle_forms rule is read",
         R"({"battle_forms":[{"class":2,"attacks":3,"strikes":[{},{"form":42,"attack":7},{"form":"test-mod.dragon-knight"}]}]})",
         true},
        {"battle_forms that is not a list", R"({"battle_forms":{"class":2}})", false},
        {"a battle_forms rule without a class", R"({"battle_forms":[{"strikes":[{}]}]})", false},
        {"a battle_forms class past the table", R"({"battle_forms":[{"class":165,"strikes":[{}]}]})", false},
        {"a battle_forms rule with no strikes", R"({"battle_forms":[{"class":2,"strikes":[]}]})", false},
        {"a battle_forms rule with ten strikes",
         R"({"battle_forms":[{"class":2,"strikes":[{},{},{},{},{},{},{},{},{},{}]}]})", false},
        {"a battle_forms form of class 0", R"({"battle_forms":[{"class":2,"strikes":[{"form":0}]}]})", false},
        {"a battle_forms attack past the attack table",
         R"({"battle_forms":[{"class":2,"strikes":[{"attack":159}]}]})", false},
        {"a battle_forms attack count of 10", R"({"battle_forms":[{"class":2,"attacks":10,"strikes":[{}]}]})", false},
        {"a battle_forms field the port does not know",
         R"({"battle_forms":[{"class":2,"strikes":[{"form":42,"hp":5}]}]})", false},
        {"a battle_forms rule with a random form list and a breath is read",
         R"({"battle_forms":[{"class":"test-mod.dragon-knight","repeat":true,"strikes":[{"form":[57,58,"test-mod.wyrm"],"attack":"breath"}]}]})",
         true},
        {"a battle_forms form list that is empty", R"({"battle_forms":[{"class":2,"strikes":[{"form":[]}]}]})", false},
        {"a battle_forms form list of 33 classes",
         R"({"battle_forms":[{"class":2,"strikes":[{"form":[)" + [] {
             std::string list;
             for (int i = 0; i < 33; i++) {
                 list += (i ? "," : "") + std::string("57");
             }
             return list;
         }() + R"(]}]}]})",
         false},
        {"a battle_forms form list with a class past the table",
         R"({"battle_forms":[{"class":2,"strikes":[{"form":[57,200]}]}]})", false},
        {"a battle_forms attack word other than breath",
         R"({"battle_forms":[{"class":2,"strikes":[{"attack":"fire"}]}]})", false},
        {"a battle_forms repeat that is not a boolean",
         R"({"battle_forms":[{"class":2,"repeat":1,"strikes":[{}]}]})", false},
        {"broken JSON", R"({"classes":)", false},
        {"a document 100000 levels deep", nested(100000), false},
        {"a file over the size limit", std::string(kMaxPackFileSize + 1, ' '), false},
        {"a record id past the table", R"({"classes":{"165":{"str_base":1}}})", false},
        {"a negative record id", R"({"items":{"-1":{"price":1}}})", false},
        {"a value too large for its field", R"({"classes":{"34":{"str_growth":256}}})", false},
        {"a float value", R"({"items":{"1":{"price":1.5}}})", false},
        {"a bytes edit past the record", R"({"items":{"1":{"bytes":{"0x1F":"0102"}}}})", false},
        {"a bytes offset far outside", R"({"items":{"1":{"bytes":{"0x7FFFFFFF":"01"}}}})", false},
        {"a whole record of the wrong size", R"({"items":{"1":{"record":"00"}}})", false},
        {"hex that is not hex", R"({"items":{"1":{"bytes":{"0":"zz"}}}})", false},
        {"a name with a control character", R"({"items":{"1":{"name":"a\u0001b"}}})", false},
        {"a name too long", R"({"items":{"1":{"name":")" + std::string(40, 'a') + R"("}}})", false},
        {"a name for a table without names", R"({"rewards":{"0":{"name":"x"}}})", false},
        {"an unknown table", R"({"weapons":{}})", false},
        {"a new class with copy_of is read",
         R"({"classes":{"test-mod.dragon-knight":{"copy_of":34,"name":"Dragon Knight","str_base":120}}})", true},
        {"a new class without copy_of", R"({"classes":{"test-mod.dragon-knight":{"str_base":120}}})", false},
        {"a new class in Change Class is read",
         R"({"classes":{"test-mod.dragon-knight":{"copy_of":34,"change_class":{"family_of":2,"str":40,"alignment_max":255}}}})",
         true},
        {"a change_class block that is not an object",
         R"({"classes":{"test-mod.dragon-knight":{"copy_of":34,"change_class":5}}})", false},
        {"a change_class field the port does not know",
         R"({"classes":{"test-mod.dragon-knight":{"copy_of":34,"change_class":{"luck":3}}}})", false},
        {"a change_class requirement past 255",
         R"({"classes":{"test-mod.dragon-knight":{"copy_of":34,"change_class":{"str":300}}}})", false},
        {"a new class with art files is read",
         R"({"classes":{"test-mod.dragon-knight":{"copy_of":34,"art":{"portrait":"art/portrait.png","figure_back":"art/b.png"}}}})",
         true},
        {"an art path that climbs out of the pack",
         R"({"classes":{"test-mod.dragon-knight":{"copy_of":34,"art":{"portrait":"../../saves/x.png"}}}})", false},
        {"an art path from the root",
         R"({"classes":{"test-mod.dragon-knight":{"copy_of":34,"art":{"portrait":"/etc/x.png"}}}})", false},
        {"an art path with a drive letter",
         R"({"classes":{"test-mod.dragon-knight":{"copy_of":34,"art":{"portrait":"C:/x.png"}}}})", false},
        {"an art path with a backslash",
         R"({"classes":{"test-mod.dragon-knight":{"copy_of":34,"art":{"portrait":"art\\..\\x.png"}}}})", false},
        {"an art file that is not a PNG",
         R"({"classes":{"test-mod.dragon-knight":{"copy_of":34,"art":{"portrait":"art/portrait.exe"}}}})", false},
        {"a battle art folder is read",
         R"({"classes":{"test-mod.dragon-knight":{"copy_of":34,"art":{"battle":"art/battle"}}}})", true},
        {"a battle art folder that climbs out of the pack",
         R"({"classes":{"test-mod.dragon-knight":{"copy_of":34,"art":{"battle":"art/../../x"}}}})", false},
        {"an art field the port does not know",
         R"({"classes":{"test-mod.dragon-knight":{"copy_of":34,"art":{"sound":"art/b.png"}}}})", false},
        {"a change_class family_of past the standard classes",
         R"({"classes":{"test-mod.dragon-knight":{"copy_of":34,"change_class":{"family_of":120}}}})", false},
        {"a new class copying an id past the ROM's classes",
         R"({"classes":{"test-mod.dragon-knight":{"copy_of":200}}})", false},
        {"a new class key in a table not indexed by class", R"({"items":{"test-mod.sword":{"price":1}}})", false},
        {"a malformed class key", R"({"classes":{"Bad Key!":{"copy_of":34}}})", false},
        {"a LordlyCaliber project with a version of the wrong type",
         R"({"format":"LordlyCaliberProject","version":"x"})", false},
        {"a LordlyCaliber project with records of the wrong type",
         R"({"format":"LordlyCaliberProject","version":1,"records":[1,2]})", false},
        {"a LordlyCaliber name of the wrong type",
         R"({"format":"LordlyCaliberProject","version":1,"itemNames":{"1":5}})", false},
    };
}

void write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// Mod folders: the load order (priority, then name, then requirements) and
// every reason a mod is refused.
int run_mod_folders() {
    namespace files = ogre::content::files;
    const std::filesystem::path base = ogre::host::exe_dir();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    const std::filesystem::path user = base / "user-content";
    write(base / "content" / "shipped.json", "{}");
    write(user / "a-loose.json", "{}");
    write(user / "early" / "mod.json", R"({"id":"early","priority":-5})");
    write(user / "early" / "pack.json", "{}");
    write(user / "needs-later" / "mod.json", R"({"id":"needs-later","requires":["later"]})");
    write(user / "needs-later" / "script.lua", "");
    write(user / "later" / "mod.json", R"({"id":"later"})");
    write(user / "later" / "pack.json", "{}");
    write(user / "off" / "mod.json", R"({"id":"off","enabled":false})");
    write(user / "off" / "pack.json", "{}");
    write(user / "bad-id" / "mod.json", R"({"id":"Bad Id!"})");
    write(user / "dup-1" / "mod.json", R"({"id":"same"})");
    write(user / "dup-2" / "mod.json", R"({"id":"same"})");
    write(user / "missing" / "mod.json", R"({"id":"missing","requires":["nowhere"]})");
    write(user / "cycle-a" / "mod.json", R"({"id":"cycle-a","requires":["cycle-b"]})");
    write(user / "cycle-b" / "mod.json", R"({"id":"cycle-b","requires":["cycle-a"]})");
    write(user / "broken" / "mod.json", R"({"id":)");
    write(user / "wrong-type" / "mod.json", R"({"id":"wrong-type","priority":"high"})");
    write(user / "huge" / "mod.json", R"({"id":"huge","name":")" + std::string(70000, 'x') + R"("})");
    write(user / "deep" / "mod.json", nested(30000));
    write(user / "lc" / "project.json", "{}");
    write(user / "examples-like" / "ignored.json", "{}");

    const files::Listing listing = files::scan();
    std::string order;
    for (const files::Entry& entry : listing.entries) {
        order += entry.id + " ";
    }
    const std::string expected =
        "content/shipped.json user-content/early/pack.json user-content/a-loose.json user-content/later/pack.json "
        "user-content/lc/project.json user-content/needs-later/script.lua ";
    int failed = 0;
    const bool order_ok = order == expected;
    failed += order_ok ? 0 : 1;
    std::printf("%s  mod folders load in priority, name and requirement order%s%s\n", order_ok ? "pass" : "FAIL",
                order_ok ? "" : ": ", order_ok ? "" : order.c_str());
    const auto refused = [&](const char* folder, const char* reason) {
        for (const files::Mod& mod : listing.mods) {
            if (mod.folder == std::string("user-content/") + folder) {
                const bool pass = mod.error.find(reason) != std::string::npos;
                std::printf("%s  mod %s is refused: %s\n", pass ? "pass" : "FAIL", folder, mod.error.c_str());
                return pass;
            }
        }
        std::printf("FAIL  mod %s is not listed\n", folder);
        return false;
    };
    failed += refused("bad-id", "needs an \"id\"") ? 0 : 1;
    failed += refused("dup-1", "has this id") && refused("dup-2", "has this id") ? 0 : 1;
    failed += refused("missing", "requires nowhere") ? 0 : 1;
    failed += refused("cycle-a", "requires") && refused("cycle-b", "requires") ? 0 : 1;
    failed += refused("broken", "mod.json:") ? 0 : 1;
    failed += refused("wrong-type", "mod.json:") ? 0 : 1;
    failed += refused("huge", "larger than 64 KiB") ? 0 : 1;
    failed += refused("deep", "nests deeper") ? 0 : 1;
    std::filesystem::remove_all(base, ec);
    return failed;
}

// mod-settings.json (the MODS tab's choices): the player's order and on/off
// apply, a mod held by the database stays as it was and is marked pending, and
// a broken or hostile file is ignored as a whole and the defaults load.
int run_mod_settings() {
    namespace files = ogre::content::files;
    const std::filesystem::path base = ogre::host::exe_dir();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    const std::filesystem::path user = base / "user-content";
    for (const char* id : {"a", "b", "c", "d", "needs-b"}) {
        write(user / id / "pack.json", "{}");
    }
    write(user / "a" / "mod.json", R"({"id":"a","description":"First.\nSecond line."})");
    write(user / "b" / "mod.json", R"({"id":"b"})");
    write(user / "c" / "mod.json", R"({"id":"c"})");
    write(user / "d" / "mod.json", R"({"id":"d","enabled":false})");
    write(user / "needs-b" / "mod.json", R"({"id":"needs-b","requires":["b"]})");
    const std::filesystem::path settings = files::settings_path();
    const auto order_of = [](const files::Listing& listing) {
        std::string order;
        for (const files::Entry& entry : listing.entries) {
            order += entry.mod + " ";
        }
        return order;
    };
    const auto find = [](const files::Listing& listing, const char* id) -> const files::Mod* {
        for (const files::Mod& mod : listing.mods) {
            if (mod.id == id) {
                return &mod;
            }
        }
        return nullptr;
    };
    int failed = 0;
    const auto check = [&failed](bool pass, const char* name, const std::string& detail) {
        failed += pass ? 0 : 1;
        std::printf("%s  %s%s%s\n", pass ? "pass" : "FAIL", name, detail.empty() ? "" : ": ", detail.c_str());
    };
    const std::string defaults = "a b c needs-b ";

    std::filesystem::remove(settings, ec);
    files::Listing listing = files::scan();
    check(order_of(listing) == defaults && listing.settings_error.empty(), "no mod-settings.json: the defaults load",
          order_of(listing));
    const files::Mod* a = find(listing, "a");
    check(a != nullptr && a->description == "First. Second line.", "a description is read as one paragraph",
          a != nullptr ? a->description : "");

    write(settings, R"({"order":["c","a","needs-b","b"],"enabled":{"d":true,"a":false}})");
    listing = files::scan();
    check(order_of(listing) == "c d b needs-b " && listing.settings_error.empty(),
          "the player's order and on/off apply; a mod still loads after its requirement", order_of(listing));
    const files::Mod* d = find(listing, "d");
    a = find(listing, "a");
    check(d != nullptr && d->chosen && d->wanted && !d->enabled_default && a != nullptr && a->chosen && !a->wanted,
          "each switched mod is marked as the player's choice", "");
    bool sorted = true;
    for (size_t i = 0; i < listing.mods.size(); i++) {
        sorted = sorted && listing.mods[i].order == static_cast<int>(i);
    }
    check(sorted && listing.mods.front().id == "c", "the mods are listed in load order", listing.mods.front().id);

    listing = files::scan({{"d", false}});
    d = find(listing, "d");
    bool idle = false;
    for (const files::Entry& entry : listing.idle) {
        idle = idle || entry.mod == "d";
    }
    check(order_of(listing) == "c b needs-b " && d != nullptr && d->pending && !d->enabled && idle,
          "a held mod keeps its state and is pending", order_of(listing));

    write(settings, R"({"enabled":{"b":false}})");
    listing = files::scan();
    const files::Mod* needs = find(listing, "needs-b");
    check(needs != nullptr && needs->error.find("requires b") != std::string::npos,
          "turning off a requirement refuses the mod that needs it", needs != nullptr ? needs->error : "");

    struct Hostile {
        const char* name;
        std::string text;
    };
    std::string many = R"({"order":[)";
    for (size_t i = 0; i <= files::kMaxSettingsIds; i++) {
        many += (i == 0 ? "\"m" : ",\"m") + std::to_string(i) + "\"";
    }
    many += "]}";
    const Hostile hostile[] = {
        {"broken JSON", R"({"order":)"},
        {"not an object", R"(["a","b"])"},
        {"\"order\" is not a list", R"({"order":"a"})"},
        {"an id that is not a mod id", R"({"order":["../../x"]})"},
        {"an id listed twice", R"({"order":["a","a"]})"},
        {"a switch that is not true or false", R"({"enabled":{"a":1}})"},
        {"more than 1024 ids", many},
        {"a document 100000 levels deep", nested(100000)},
        {"a document 30000 levels deep, under 64 KiB", nested(30000)},
        {"a file over 64 KiB", R"({"order":[],"_":")" + std::string(70000, 'x') + R"("})"},
    };
    for (const Hostile& h : hostile) {
        write(settings, h.text);
        try {
            listing = files::scan();
            check(order_of(listing) == defaults && !listing.settings_error.empty(),
                  (std::string("mod-settings.json with ") + h.name + " is ignored").c_str(), listing.settings_error);
        } catch (...) {
            check(false, (std::string("mod-settings.json with ") + h.name + " is ignored").c_str(),
                  "an exception left scan");
        }
    }
    std::filesystem::remove(settings, ec);
    std::filesystem::create_directories(settings, ec);
    listing = files::scan();
    check(order_of(listing) == defaults && !listing.settings_error.empty(),
          "a folder named mod-settings.json is ignored", listing.settings_error);
    std::filesystem::remove_all(settings, ec);

    // A loose file in user-content/ is an item of its own ("file:<name>"):
    // the player can switch it, order it and delete it.
    write(user / "zz.json", "{}");
    listing = files::scan();
    check(order_of(listing) == defaults + "file:zz.json ", "a loose file is an item with its own id", order_of(listing));
    write(settings, R"({"order":["file:zz.json","a"],"enabled":{"b":false,"needs-b":false}})");
    listing = files::scan();
    // The two listed items swap their places; c keeps its own.
    check(order_of(listing) == "file:zz.json c a ", "a loose file is ordered like a mod", order_of(listing));
    write(settings, R"({"enabled":{"file:zz.json":false}})");
    listing = files::scan();
    check(order_of(listing) == defaults, "a loose file is switched off like a mod", order_of(listing));
    for (const char* key : {"file:../x.json", "file:a\\\\b.json", "file:", "file:.."}) {
        write(settings, std::string(R"({"enabled":{")") + key + R"(":false}})");
        listing = files::scan();
        check(!listing.settings_error.empty(), (std::string("the key ") + key + " is refused").c_str(),
              listing.settings_error);
    }
    std::filesystem::remove(settings, ec);
    listing = files::scan();
    const files::Mod* zz = find(listing, "file:zz.json");
    std::string erase_error;
    check(zz != nullptr && files::erase(*zz, erase_error) && !std::filesystem::exists(user / "zz.json"),
          "erase deletes a loose file in user-content/", erase_error);
    const files::Mod* c = find(listing, "c");
    check(c != nullptr && files::erase(*c, erase_error) && !std::filesystem::exists(user / "c"),
          "erase deletes a mod folder in user-content/", erase_error);
    files::Mod outside;
    outside.path = base / "content" / "shipped.json";
    write(outside.path, "{}");
    check(!files::erase(outside, erase_error) && std::filesystem::exists(outside.path),
          "erase refuses a file outside user-content/", erase_error);
    outside.path = user / "a" / "pack.json";
    check(!files::erase(outside, erase_error) && std::filesystem::exists(outside.path),
          "erase refuses a file inside a mod folder", erase_error);
    outside.path = user;
    check(!files::erase(outside, erase_error) && std::filesystem::exists(user),
          "erase refuses user-content/ itself", erase_error);

    files::Settings written;
    written.order = {"b", "a"};
    written.enabled["c"] = false;
    files::Settings back;
    std::string error;
    const bool round = files::write_settings(written);
    back = files::read_settings(error);
    check(round && error.empty() && back.order == written.order && back.enabled == written.enabled,
          "write_settings writes what read_settings reads", error);
    std::filesystem::remove_all(base, ec);
    return failed;
}

// mod.json "url": the MODS tab opens it in the web browser on a click, so only
// an http:// or https:// address of at most 200 characters is accepted, and a
// mod with any other "url" is refused.
int run_mod_urls() {
    namespace files = ogre::content::files;
    const std::filesystem::path base = ogre::host::exe_dir();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    struct Url {
        const char* folder;
        std::string json;  // the "url" value as JSON
        bool accept;
    };
    const Url urls[] = {
        {"https", R"("https://github.com/topics/ob64-mod")", true},
        {"http", R"("http://example.com/my-mod?v=1#top")", true},
        {"file", R"("file:///C:/Windows/System32/calc.exe")", false},
        {"javascript", R"x("javascript:alert(1)")x", false},
        {"no-scheme", R"("github.com/my-mod")", false},
        {"bare-scheme", R"("https://")", false},
        {"space", R"("https://example.com/a b")", false},
        {"quote", R"("https://example.com/\"onclick")", false},
        {"control", R"("https://example.com/\u0007")", false},
        {"overlong", "\"https://example.com/" + std::string(200, 'a') + "\"", false},
        {"number", "42", false},
    };
    for (const Url& u : urls) {
        write(base / "user-content" / u.folder / "mod.json",
              std::string(R"({"id":")") + u.folder + R"(","url":)" + u.json + "}");
    }
    const files::Listing listing = files::scan();
    int failed = 0;
    for (const Url& u : urls) {
        const auto found = std::find_if(listing.mods.begin(), listing.mods.end(),
                                        [&](const files::Mod& m) { return m.folder == std::string("user-content/") + u.folder; });
        const bool loaded = found != listing.mods.end() && found->error.empty() && !found->url.empty();
        const bool refused = found != listing.mods.end() && !found->error.empty() && found->url.empty();
        const bool pass = u.accept ? loaded : refused;
        failed += pass ? 0 : 1;
        std::printf("%s  a mod.json url (%s) is %s\n", pass ? "pass" : "FAIL", u.folder,
                    u.accept ? "kept" : "refused");
    }
    std::filesystem::remove_all(base, ec);
    return failed;
}

// The shipped example mods (app/content/examples/, passed by app/CMakeLists.txt):
// copied into user-content/ together, every mod loads and every pack parses.
int run_examples(const std::filesystem::path& examples) {
    namespace files = ogre::content::files;
    const std::filesystem::path base = ogre::host::exe_dir();
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(base / "user-content", ec);
    std::filesystem::copy(examples, base / "user-content",
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                          ec);
    int failed = 0;
    if (ec) {
        std::printf("FAIL  copy the examples from %s: %s\n", examples.string().c_str(), ec.message().c_str());
        return 1;
    }
    const files::Listing listing = files::scan();
    int mods = 0;
    for (const files::Mod& mod : listing.mods) {
        if (mod.kind != files::ModKind::Folder) {
            continue;
        }
        mods++;
        const bool pass = mod.error.empty() && mod.enabled && !mod.description.empty();
        failed += pass ? 0 : 1;
        std::printf("%s  example %s loads and has a description%s%s\n", pass ? "pass" : "FAIL", mod.folder.c_str(),
                    mod.error.empty() ? "" : ": ", mod.error.c_str());
    }
    for (const files::Entry& entry : listing.entries) {
        if (entry.kind != files::Kind::Pack) {
            continue;
        }
        std::ifstream file(entry.path, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::string error;
        const bool pass = parse_pack(text, entry.id, error).has_value();
        failed += pass ? 0 : 1;
        std::printf("%s  example pack %s parses%s%s\n", pass ? "pass" : "FAIL", entry.id.c_str(),
                    error.empty() ? "" : ": ", error.c_str());
    }
    const bool any = mods >= 5;
    failed += any ? 0 : 1;
    std::printf("%s  %d example mods found\n", any ? "pass" : "FAIL", mods);
    std::filesystem::remove_all(base, ec);
    return failed;
}

// Keys for the classes a LordlyCaliber project adds: valid class keys, one
// per project and id, so two projects that both add class 165 do not collide.
int run_lordlycaliber_keys() {
    struct Key {
        std::string pack_id;
        int lc_id;
        std::string expected;
    };
    const std::string long_name(80, 'a');
    const Key keys[] = {
        {"user-content/my-project/project.json", 165, "lc.my-project.class-165"},
        {"user-content/other/project.json", 165, "lc.other.class-165"},
        {"user-content/My Project!/project.json", 166, "lc.my-project-.class-166"},
        {"user-content/loose-project.json", 170, "lc.loose-project.class-170"},
        {"user-content/mod/lc/project.json", 247, "lc.lc.class-247"},
        {"user-content/!!!/project.json", 165, "lc.---.class-165"},
        {"project.json", 165, "lc.project.class-165"},
        {"user-content/" + long_name + "/project.json", 165,
         "lc." + std::string(64 - 3 - 10, 'a') + ".class-165"},
    };
    int failed = 0;
    for (const Key& k : keys) {
        const std::string key = lordlycaliber_class_key(k.pack_id, k.lc_id);
        const bool pass = key == k.expected && is_class_key(key);
        failed += pass ? 0 : 1;
        std::printf("%s  LordlyCaliber class %d of %s is %s\n", pass ? "pass" : "FAIL", k.lc_id,
                    k.pack_id.c_str(), key.c_str());
    }
    return failed;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    int failed = 0;
    for (const Case& c : cases()) {
        std::string error;
        bool accepted = false;
        try {
            accepted = parse_pack(c.text, "test", error).has_value();
        } catch (...) {
            error = "an exception left parse_pack";
            accepted = !c.accept;  // fail either way
            failed++;
            std::printf("FAIL  %s: %s\n", c.name, error.c_str());
            continue;
        }
        const bool pass = accepted == c.accept;
        failed += pass ? 0 : 1;
        std::printf("%s  %s%s%s\n", pass ? "pass" : "FAIL", c.name, error.empty() ? "" : ": ",
                    error.substr(0, 80).c_str());
    }
    failed += run_mod_folders();
    failed += run_mod_settings();
    failed += run_mod_urls();
    if (argc > 1) {
        failed += run_examples(argv[1]);
    }
    failed += run_lordlycaliber_keys();
    // game.recruit's own checks (recruit.hpp; security.md section 3.1).
    struct Recruit {
        const char* name;
        int64_t cls;
        int64_t level;
        bool accept;
    };
    const Recruit recruits[] = {
        {"a recruit of class 172 at level 5", 172, 5, true},
        {"a recruit of class 0", 0, 5, false},
        {"a recruit of class 255", 255, 5, false},
        {"a recruit of a negative class", -3, 5, false},
        {"a recruit at level 0", 2, 0, false},
        {"a recruit at level 51", 2, 51, false},
        {"a recruit at a huge level", 2, int64_t{1} << 40, false},
    };
    for (const Recruit& r : recruits) {
        const bool pass = ogre::recruit::valid(r.cls, r.level) == r.accept;
        failed += pass ? 0 : 1;
        std::printf("%s  %s\n", pass ? "pass" : "FAIL", r.name);
    }
    std::printf("Content pack hostile-file test: %d failed\n", failed);
    return failed == 0 ? 0 : 1;
}
