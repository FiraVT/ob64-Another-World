// The Esc menu drawn with RmlUi. See options_view.hpp.

#include "options_view.hpp"

#include <atomic>
#include <cstdio>
#include <mutex>

#include <RmlUi/Core.h>

#include "options.hpp"

namespace ogre::options_view {
namespace {

// The data model mirrors Snapshot with RmlUi-registered types.
struct ModelOption {
    int index = 0;
    Rml::String label;
    bool current = false;
};

struct ModelRow {
    int index = 0;
    Rml::String kind;
    Rml::String left;
    Rml::String right;
    Rml::String note;
    Rml::String tone;
    bool on = false;
    Rml::String more;
    Rml::String number;
    bool armed = false;
    bool first = false;
    bool last = false;
    int cell = -1;
    bool drop = false;  // a dragged mod row is over this one
    bool dragging = false;
    bool selectable = false;
    bool selected = false;
    Rml::Vector<ModelOption> options;
};

struct ModelTab {
    int index = 0;
    Rml::String label;
    bool enabled = false;
    bool active = false;
};

struct Model {
    Rml::Vector<ModelTab> tabs;
    Rml::Vector<ModelRow> rows;
    Rml::String hint;
    bool capturing = false;
    Rml::String confirm;
    int confirm_row = -1;
    bool confirm_allowed = true;
};

struct State {
    Model model;
    Rml::DataModelHandle handle;
    Rml::ElementDocument* document = nullptr;
    Rml::Context* context = nullptr;
    std::string path;
    std::atomic<bool> ready{false};
    std::atomic<bool> visible{false};

    int drag_from = -1;  // render thread: the mod row being dragged

    std::mutex mutex;  // guards the two fields below
    Snapshot pending;
    bool dirty = false;
    std::vector<Command> commands;
};

State& state() {
    static State s;
    return s;
}

void push(Command command) {
    // One line per click, so a playtest log shows which clicks reached the menu.
    std::fprintf(stderr, "[options] click kind=%d a=%d b=%d\n", static_cast<int>(command.kind), command.a, command.b);
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.commands.push_back(command);
}

void apply_snapshot(State& s, const Snapshot& snap) {
    Model& m = s.model;
    m.tabs.clear();
    for (size_t i = 0; i < snap.tabs.size(); i++) {
        if (snap.tabs[i].label.empty()) {
            continue;  // a hidden tab (SAVES); the index stays the tab's id
        }
        m.tabs.push_back(ModelTab{static_cast<int>(i), snap.tabs[i].label, snap.tabs[i].enabled, snap.tabs[i].active});
    }
    m.rows.clear();
    for (size_t i = 0; i < snap.rows.size(); i++) {
        const Row& r = snap.rows[i];
        ModelRow row;
        row.index = static_cast<int>(i);
        row.kind = r.kind;
        row.left = r.left;
        row.right = r.right;
        row.note = r.note;
        row.tone = r.tone;
        row.on = r.on;
        row.more = r.more;
        row.number = r.number;
        row.armed = r.armed;
        row.first = r.first;
        row.last = r.last;
        row.cell = r.cell;
        row.selectable = r.selectable;
        row.selected = r.selected;
        for (size_t o = 0; o < r.options.size(); o++) {
            row.options.push_back(ModelOption{static_cast<int>(o), r.options[o].label, r.options[o].current});
        }
        m.rows.push_back(std::move(row));
    }
    m.hint = snap.hint;
    m.capturing = snap.capturing;
    m.confirm = snap.confirm;
    m.confirm_row = snap.confirm_row;
    m.confirm_allowed = snap.confirm_allowed;
    s.handle.DirtyAllVariables();
}

}  // namespace

bool init(Rml::Context* context, const std::filesystem::path& asset_dir) {
    State& s = state();
    Rml::DataModelConstructor ctor = context->CreateDataModel("options");
    if (!ctor) {
        return false;
    }
    if (auto option = ctor.RegisterStruct<ModelOption>()) {
        option.RegisterMember("index", &ModelOption::index);
        option.RegisterMember("label", &ModelOption::label);
        option.RegisterMember("current", &ModelOption::current);
    }
    ctor.RegisterArray<Rml::Vector<ModelOption>>();
    if (auto row = ctor.RegisterStruct<ModelRow>()) {
        row.RegisterMember("index", &ModelRow::index);
        row.RegisterMember("kind", &ModelRow::kind);
        row.RegisterMember("left", &ModelRow::left);
        row.RegisterMember("right", &ModelRow::right);
        row.RegisterMember("note", &ModelRow::note);
        row.RegisterMember("tone", &ModelRow::tone);
        row.RegisterMember("on", &ModelRow::on);
        row.RegisterMember("more", &ModelRow::more);
        row.RegisterMember("number", &ModelRow::number);
        row.RegisterMember("armed", &ModelRow::armed);
        row.RegisterMember("first", &ModelRow::first);
        row.RegisterMember("last", &ModelRow::last);
        row.RegisterMember("cell", &ModelRow::cell);
        row.RegisterMember("drop", &ModelRow::drop);
        row.RegisterMember("dragging", &ModelRow::dragging);
        row.RegisterMember("selectable", &ModelRow::selectable);
        row.RegisterMember("selected", &ModelRow::selected);
        row.RegisterMember("options", &ModelRow::options);
    }
    ctor.RegisterArray<Rml::Vector<ModelRow>>();
    if (auto tab = ctor.RegisterStruct<ModelTab>()) {
        tab.RegisterMember("index", &ModelTab::index);
        tab.RegisterMember("label", &ModelTab::label);
        tab.RegisterMember("enabled", &ModelTab::enabled);
        tab.RegisterMember("active", &ModelTab::active);
    }
    ctor.RegisterArray<Rml::Vector<ModelTab>>();
    ctor.Bind("tabs", &s.model.tabs);
    ctor.Bind("rows", &s.model.rows);
    ctor.Bind("hint", &s.model.hint);
    ctor.Bind("capturing", &s.model.capturing);
    ctor.Bind("confirm", &s.model.confirm);
    ctor.Bind("confirm_row", &s.model.confirm_row);
    ctor.Bind("confirm_allowed", &s.model.confirm_allowed);

    ctor.BindEventCallback("tab", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) {
            push({Command::Kind::Tab, args[0].Get<int>(), 0});
        }
    });
    ctor.BindEventCallback("activate", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) {
            push({Command::Kind::Activate, args[0].Get<int>(), 0});
        }
    });
    ctor.BindEventCallback("choose", [](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& args) {
        if (args.size() >= 2) {
            push({Command::Kind::Choose, args[0].Get<int>(), args[1].Get<int>()});
            event.StopPropagation();  // the row's own click must not also step it
        }
    });
    ctor.BindEventCallback("close", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        push({Command::Kind::Close, 0, 0});
    });
    // Dragging a mod row (options.rml `drag: drag-drop`): the rows it passes
    // over are marked on the render thread; the drop goes to the overlay as a
    // command, which moves the mod in the load order.
    const auto mark = [](int from, int over) {
        State& st = state();
        for (ModelRow& row : st.model.rows) {
            row.dragging = row.index == from;
            row.drop = row.index == over && over != from;
        }
        st.handle.DirtyVariable("rows");
    };
    ctor.BindEventCallback("drag_start", [mark](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) {
            state().drag_from = args[0].Get<int>();
            mark(state().drag_from, -1);
        }
    });
    ctor.BindEventCallback("drag_over", [mark](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty() && state().drag_from >= 0) {
            mark(state().drag_from, args[0].Get<int>());
        }
    });
    ctor.BindEventCallback("drag_drop", [mark](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty() && state().drag_from >= 0) {
            push({Command::Kind::Drop, state().drag_from, args[0].Get<int>()});
        }
        state().drag_from = -1;
        mark(-1, -1);
    });
    ctor.BindEventCallback("drag_end", [mark](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        state().drag_from = -1;
        mark(-1, -1);
    });
    s.handle = ctor.GetModelHandle();
    s.context = context;
    s.path = (asset_dir / "options.rml").string();
    s.document = context->LoadDocument(s.path);
    std::fprintf(stderr, "[options] menu %s\n", s.document != nullptr ? "loaded" : "missing");
    s.ready.store(s.document != nullptr);
    return s.document != nullptr;
}

void update() {
    State& s = state();
    if (s.document == nullptr) {
        return;
    }
    const bool want = s.visible.load();
    if (want && !s.document->IsVisible()) {
        // Re-read options.rml/.rcss on every open, like the Organize view.
        if (Rml::ElementDocument* fresh = (Rml::Factory::ClearStyleSheetCache(), s.context->LoadDocument(s.path))) {
            s.context->UnloadDocument(s.document);
            s.document = fresh;
        }
        s.document->Show();
    }
    else if (!want && s.document->IsVisible()) {
        s.document->Hide();
    }
    if (!want) {
        return;
    }
    // UI SCALE and HIGH CONTRAST (GRAPHICS tab) as classes on the menu
    // (options.rcss); they apply as soon as the row changes.
    if (Rml::Element* menu = s.document->GetElementById("menu")) {
        menu->SetClass("large", ogre::choice_on(ogre::Choice::UiScale));
        menu->SetClass("contrast", ogre::choice_on(ogre::Choice::Contrast));
    }
    Snapshot snap;
    bool fresh = false;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (s.dirty) {
            snap = std::move(s.pending);
            s.dirty = false;
            fresh = true;
        }
    }
    if (fresh) {
        apply_snapshot(s, snap);
        // Keep the keyboard/pad selection on screen.
        if (Rml::Element* selected = s.document->QuerySelector(".row.selected")) {
            selected->ScrollIntoView(false);
        }
    }
}

bool available() {
    return state().ready.load();
}

void set_visible(bool visible) {
    state().visible.store(visible);
}

bool visible() {
    return state().visible.load();
}

void publish(Snapshot snapshot) {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.pending = std::move(snapshot);
    s.dirty = true;
}

std::vector<Command> take_commands() {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    std::vector<Command> out;
    out.swap(s.commands);
    return out;
}

}  // namespace ogre::options_view

