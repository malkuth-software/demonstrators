#include "graph_root.hpp"

#include <gui_forms/event.hpp>
#include <gui_forms/surface_root/surface_root.hpp>
#include <gui_forms/types/painter/painter.hpp>

#include <algorithm>
#include <cstdio>
#include <span>
#include <string_view>

namespace node_graph {

namespace gf = gui_forms;

namespace {

constexpr std::string_view menu_paper = "node-graph.menu.paper";
constexpr std::string_view menu_blue = "node-graph.menu.blue";
constexpr std::string_view menu_green = "node-graph.menu.green";
constexpr std::string_view menu_grey = "node-graph.menu.grey";
constexpr std::string_view menu_soft = "node-graph.menu.soft";
constexpr std::string_view menu_routed = "node-graph.menu.routed";
constexpr std::string_view menu_add = "node-graph.menu.add";
constexpr std::string_view menu_frame = "node-graph.menu.frame";

void append_count(std::string& out, std::uint64_t value) {
    char buffer[32]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    if (written > 0) out.append(buffer, static_cast<std::size_t>(written));
}

void append_decimal(std::string& out, double value) {
    char buffer[32]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%.3g", value);
    if (written > 0) out.append(buffer, static_cast<std::size_t>(written));
}

std::size_t live_count_of_nodes(const gf::NodeGraph& graph) noexcept {
    std::size_t count = 0U;
    for (const gf::NodeRecord& node : graph.node_slots()) {
        if (node.live) ++count;
    }
    return count;
}

std::size_t live_count_of_links(const gf::NodeGraph& graph) noexcept {
    std::size_t count = 0U;
    for (const gf::LinkRecord& link : graph.link_slots()) {
        if (link.live) ++count;
    }
    return count;
}

} // namespace

GraphRoot::GraphRoot(gf::StableId stable_id)
    : gf::Control(std::move(stable_id)), evaluator_(document_), menu_("node-graph.menu") {
    status_text_.reserve(256U);
    palette_kinds_.reserve(maximum_kinds);
    edit_before_.reserve(256U);
}

GraphRoot::~GraphRoot() {
    // The palette may outlive this control (a shared child): unbind the row
    // targets before their object goes.
    if (palette_ != nullptr) (*palette_).set_row_callbacks(gf::VirtualRowsCallbacks{});
}

void GraphRoot::initialize_control_tree() {
    canvas_ = gf::make_control<NodeCanvasView>(gf::StableId("node-graph.canvas"), document_, evaluator_);
    status_ = gf::make_control<gf::Label>(gf::StableId("node-graph.status"));
    palette_ = gf::make_control<gf::VirtualRowsView>(gf::StableId("node-graph.palette"));
    (*palette_).set_estimated_row_height(palette_row_height);
    (*palette_).visible = false;
    gf::VirtualRowsCallbacks callbacks{};
    callbacks.measure = gf::Delegate<gf::VirtualRowMeasure&>::bind<GraphRoot, &GraphRoot::measure_palette_row>(*this);
    callbacks.paint = gf::Delegate<gf::VirtualRowPaint&>::bind<GraphRoot, &GraphRoot::paint_palette_row>(*this);
    callbacks.describe =
        gf::Delegate<gf::VirtualRowDescription&>::bind<GraphRoot, &GraphRoot::describe_palette_row>(*this);
    (*palette_).set_row_callbacks(callbacks);

    add_child(canvas_);
    add_child(status_);
    add_child(palette_);

    gf::on((*canvas_).edit_requested(), *this, &GraphRoot::on_edit_requested);
    gf::on((*canvas_).palette_requested(), *this, &GraphRoot::on_palette_requested);
    gf::on((*canvas_).changed(), *this, &GraphRoot::on_canvas_changed);
    gf::on((*canvas_).save_requested(), *this, &GraphRoot::on_save_requested);
    gf::on((*canvas_).context_menu_requested(), *this, &GraphRoot::on_context_request);
    gf::on((*palette_).row_activated(), *this, &GraphRoot::on_palette_activated);
    gf::on(menu_.item_invoked(), *this, &GraphRoot::on_menu_item);
    rebuild_menu();
    refresh_status();
}

// -- layout ---------------------------------------------------------------------------------

gf::Size GraphRoot::measure(gf::Size available) {
    return available;
}

void GraphRoot::arrange(gf::Rect final_bounds) {
    arrange_self(final_bounds);
    const double width = final_bounds.width;
    const double canvas_height = std::max(0.0, final_bounds.height - status_height);
    set_child_layout(canvas_, gf::Rect{0.0, 0.0, width, canvas_height});
    set_child_layout(status_, gf::Rect{0.0, canvas_height, width, status_height});
    gf::Rect palette{};
    if (palette_request_.has_value()) {
        const double height = palette_row_height * palette_rows_shown;
        // Kept inside the canvas: moved left and up when it would overflow.
        const double x = std::clamp(palette_origin_.x, 0.0, std::max(0.0, width - palette_width));
        const double y = std::clamp(palette_origin_.y, 0.0, std::max(0.0, canvas_height - height));
        palette = gf::Rect{x, y, palette_width, height};
    }
    set_child_layout(palette_, palette);
    const gf::Rect host = edit_host_rect();
    for (std::size_t index = 0U; index < edit_hosts_.size(); ++index) {
        if (edit_hosts_[index] == nullptr) continue;
        const bool open = editing_kind_.has_value() && (*editing_kind_).value == index;
        set_child_layout(edit_hosts_[index], open ? host : gf::Rect{});
    }
}

gf::Rect GraphRoot::edit_host_rect() const {
    if (!editing_.has_value()) return gf::Rect{};
    const gf::NodeRecord* node = document_.graph().node((*editing_).node);
    if (node == nullptr) return gf::Rect{};
    const gf::Rect rect = (*canvas_).view().rect_to_view((*node).bounds);
    const double width = std::max(rect.width, 120.0);
    // The title over the caption; a property just under it.
    const double y = (*editing_).title ? rect.y : rect.y + 24.0 * (*canvas_).view().zoom;
    const gf::Rect host{rect.x, y, width, edit_host_height};
    return host;
}

// -- edit hosts -----------------------------------------------------------------------------

bool GraphRoot::open_edit(const EditRequest& request) {
    if (editing_.has_value()) close_edit_host();
    const std::optional<KindIndex> kind = document_.kind_of(request.node);
    const gf::NodeRecord* node = document_.graph().node(request.node);
    if (!kind.has_value() || node == nullptr || (*kind).value >= edit_hosts_.size()) return false;
    const KindDescriptor& descriptor = kind_at(*kind);
    EditRequest effective = request;
    // A kind without a primary property edits its title.
    if (descriptor.primary_property.empty()) effective.title = true;
    std::shared_ptr<gf::TextBox>& host = edit_hosts_[(*kind).value];
    if (host == nullptr) {
        std::string id("node-graph.edit.");
        id.append(descriptor.name);
        host = gf::make_control<gf::TextBox>(gf::StableId(id));
        (*host).accepts_enter = true;
        (*host).accepts_escape = true;
        add_child(host);
        gf::on((*host).committed(), *this, &GraphRoot::on_host_committed);
        gf::on((*host).cancelled(), *this, &GraphRoot::on_host_cancelled);
        gf::on((*host).focus_observed(), *this, &GraphRoot::on_host_focus);
        ++counters_.edit_hosts_created;
    }
    edit_before_.clear();
    if (effective.title) {
        edit_before_.assign((*node).title);
    } else {
        const gf::NodeProperty* property = document_.graph().property(request.node, descriptor.primary_property);
        if (property != nullptr) edit_before_.assign((*property).value);
    }
    editing_ = effective;
    editing_kind_ = kind;
    (*host).text = edit_before_;
    (*host).visible = true;
    ++counters_.edits_opened;
    invalidate(gf::Dirty::arrange);
    gf::SurfaceRoot* root = attached_surface_root();
    if (root != nullptr) static_cast<void>((*root).request_focus(host));
    return true;
}

gf::TextBox* GraphRoot::edit_host() const noexcept {
    if (!editing_kind_.has_value()) return nullptr;
    return edit_hosts_[(*editing_kind_).value].get();
}

void GraphRoot::commit_edit(const std::string& text) {
    if (!editing_.has_value()) return;
    const EditRequest request = *editing_;
    const KindIndex kind = *editing_kind_;
    close_edit_host();
    if (text == edit_before_) {
        // Nothing changed: no step, so the document stays byte-identical.
        ++counters_.edits_unchanged;
        return;
    }
    document_.begin_gesture();
    gf::GraphResult result{};
    if (request.title) {
        result = document_.set_title(request.node, text);
    } else {
        result = document_.set_property(request.node, kind_at(kind).primary_property, text);
    }
    static_cast<void>(document_.end_gesture());
    if (result.status == gf::GraphStatus::ok) ++counters_.edits_committed;
    (*canvas_).document_changed();
    refresh_status();
}

void GraphRoot::cancel_edit() {
    if (!editing_.has_value()) return;
    close_edit_host();
    ++counters_.edits_cancelled;
}

void GraphRoot::close_edit_host() {
    gf::TextBox* host = edit_host();
    // Cleared first: hiding the focused host loses its focus, and that
    // focus loss must find nothing to commit.
    editing_.reset();
    editing_kind_.reset();
    if (host != nullptr) (*host).visible = false;
    invalidate(gf::Dirty::arrange);
    gf::SurfaceRoot* root = attached_surface_root();
    if (root != nullptr) static_cast<void>((*root).request_focus(canvas_));
}

void GraphRoot::on_edit_requested(const EditRequest& request) {
    static_cast<void>(open_edit(request));
}

void GraphRoot::on_host_committed(const std::string& text) {
    commit_edit(text);
}

void GraphRoot::on_host_cancelled() {
    cancel_edit();
}

void GraphRoot::on_host_focus(bool focused) {
    if (focused || !editing_.has_value()) return;
    gf::TextBox* host = edit_host();
    if (host == nullptr) return;
    const std::string text = (*host).text.read();
    commit_edit(text);
}

// -- palette --------------------------------------------------------------------------------

void GraphRoot::open_palette(const PaletteRequest& request) {
    palette_kinds_.clear();
    const std::span<const KindDescriptor> table = kind_table();
    std::optional<std::uint16_t> from_type{};
    if (request.from.has_value()) {
        const gf::PortRecord* port = document_.graph().port(*request.from);
        if (port != nullptr) from_type = (*port).socket_type;
    }
    for (std::size_t index = 0U; index < table.size() && index < maximum_kinds; ++index) {
        if (from_type.has_value()) {
            gf::PortIndex input{};
            if (!kind_accepts(table[index], *from_type, input)) continue;
        }
        palette_kinds_.push_back(KindIndex{static_cast<std::uint16_t>(index)});
    }
    palette_request_ = request;
    palette_origin_ = point_from_window(request.window_position);
    (*palette_).reset_rows(palette_kinds_.size());
    (*palette_).visible = true;
    ++counters_.palette_opened;
    invalidate(gf::Dirty::arrange);
    gf::SurfaceRoot* root = attached_surface_root();
    if (root != nullptr) static_cast<void>((*root).request_focus(palette_));
}

void GraphRoot::close_palette() {
    if (!palette_request_.has_value()) return;
    palette_request_.reset();
    (*palette_).visible = false;
    invalidate(gf::Dirty::arrange);
    gf::SurfaceRoot* root = attached_surface_root();
    if (root != nullptr) static_cast<void>((*root).request_focus(canvas_));
}

bool GraphRoot::insert_from_palette(std::size_t row) {
    if (!palette_request_.has_value() || row >= palette_kinds_.size()) return false;
    const PaletteRequest request = *palette_request_;
    const KindIndex kind = palette_kinds_[row];
    close_palette();
    // Placed so that the point is the new node's left middle when a link
    // brought it (its input meets the link), its top left otherwise.
    gf::WorldPoint top_left = request.world;
    if (request.from.has_value()) top_left.y -= kind_at(kind).height / 2.0;
    document_.begin_gesture();
    const gf::GraphResult added = document_.add_node(kind, top_left);
    bool linked = false;
    if (added.status == gf::GraphStatus::ok && request.from.has_value()) {
        const gf::PortRecord* from = document_.graph().port(*request.from);
        gf::PortIndex input{};
        if (from != nullptr && kind_accepts(kind_at(kind), (*from).socket_type, input)) {
            const ConnectResult connected = document_.connect(*request.from, gf::PortRef{added.node, input});
            linked = connected.status == ConnectStatus::connected;
        }
    }
    static_cast<void>(document_.end_gesture());
    if (added.status != gf::GraphStatus::ok) return false;
    ++counters_.palette_insertions;
    if (linked) ++counters_.palette_links;
    (*canvas_).document_changed();
    (*canvas_).select_only(added.node);
    refresh_status();
    return true;
}

void GraphRoot::on_palette_requested(const PaletteRequest& request) {
    open_palette(request);
}

void GraphRoot::on_palette_activated(gf::VirtualRowIndex row) {
    static_cast<void>(insert_from_palette(row.value));
}

void GraphRoot::measure_palette_row(gf::VirtualRowMeasure& row) {
    row.height = palette_row_height;
}

void GraphRoot::paint_palette_row(gf::VirtualRowPaint& row) {
    if (row.painter == nullptr || row.row.value >= palette_kinds_.size()) return;
    gf::Painter& painter = *row.painter;
    const KindDescriptor& kind = kind_at(palette_kinds_[row.row.value]);
    gf::FontSpec font{};
    font.size = 12.0;
    const gf::Color text = gf::Color::rgba(30, 34, 40);
    const gf::Color quiet = gf::Color::rgba(110, 116, 124);
    const double baseline = row.bounds.y + row.bounds.height * 0.5 + 4.0;
    painter.draw_text_utf8(gf::Point{row.bounds.x + 8.0, baseline}, kind.title, font, text);
    const std::string_view family = kind_family_name(kind.family);
    const gf::Size size = painter.measure_text_utf8(family, font);
    painter.draw_text_utf8(gf::Point{row.bounds.x + row.bounds.width - 8.0 - size.width, baseline}, family, font, quiet);
}

void GraphRoot::describe_palette_row(gf::VirtualRowDescription& row) {
    if (row.row.value >= palette_kinds_.size()) return;
    const KindDescriptor& kind = kind_at(palette_kinds_[row.row.value]);
    row.name.append(kind.title);
    row.value.append(kind_family_name(kind.family));
}

// -- canvas events, menu, status ------------------------------------------------------------

void GraphRoot::on_canvas_changed() {
    if (palette_request_.has_value() && (*canvas_).gesture() != Gesture::none) close_palette();
    if (editing_.has_value()) invalidate(gf::Dirty::arrange);
    refresh_status();
}

void GraphRoot::on_save_requested() {
    static_cast<void>(save());
}

bool GraphRoot::save() {
    if (document_.path().empty()) {
        // No file dialog without host services (GAPS.md): nothing to save to.
        ++counters_.save_failures;
        return false;
    }
    const FileResult result = document_.save_file(document_.path());
    if (result.status != FileStatus::ok) {
        ++counters_.save_failures;
        return false;
    }
    ++counters_.saves;
    refresh_status();
    return true;
}

void GraphRoot::rebuild_menu() {
    std::vector<gf::MenuItemSpec> items{};
    items.reserve(8U);
    items.push_back(gf::MenuItemSpec(std::string(menu_add), gf::MenuItemKind::command, {}, "Add node..."));
    items.push_back(gf::MenuItemSpec(std::string(menu_frame), gf::MenuItemKind::command, {}, "Frame all"));
    items.push_back(gf::MenuItemSpec(std::string(menu_soft), gf::MenuItemKind::command, {}, "Soft links"));
    items.push_back(gf::MenuItemSpec(std::string(menu_routed), gf::MenuItemKind::command, {}, "Routed links"));
    items.push_back(gf::MenuItemSpec(std::string(menu_paper), gf::MenuItemKind::command, {}, "Paper background"));
    items.push_back(gf::MenuItemSpec(std::string(menu_blue), gf::MenuItemKind::command, {}, "Blueprint background"));
    items.push_back(gf::MenuItemSpec(std::string(menu_green), gf::MenuItemKind::command, {}, "Green background"));
    items.push_back(gf::MenuItemSpec(std::string(menu_grey), gf::MenuItemKind::command, {}, "Grey background"));
    menu_.set_items(std::move(items));
}

void GraphRoot::on_context_request(gf::ContextMenuRequest& request) {
    // The canvas marks a knife release handled; that is not a menu request.
    if (request.handled) return;
    gf::Point local = request.location;
    if (request.origin != gf::ContextRequestOrigin::pointer) {
        const gf::Rect bounds = (*canvas_).client_rectangle();
        local = gf::Point{bounds.width / 2.0, bounds.height / 2.0};
    }
    menu_world_ = (*canvas_).view().to_world(local);
    const gf::MenuOpenMode mode = request.origin == gf::ContextRequestOrigin::pointer ? gf::MenuOpenMode::pointer
                                                                                      : gf::MenuOpenMode::keyboard;
    menu_.show(canvas_, (*canvas_).point_to_window(local), mode);
    request.handled = true;
}

void GraphRoot::on_menu_item(const gf::MenuItemInvocation& invocation) {
    const std::string_view item = invocation.item_id;
    if (item == menu_add) {
        PaletteRequest request{};
        request.world = menu_world_;
        request.window_position = (*canvas_).point_to_window((*canvas_).view().to_view(menu_world_));
        open_palette(request);
    } else if (item == menu_frame) {
        (*canvas_).frame_all();
    } else if (item == menu_soft) {
        (*canvas_).set_link_style(LinkStyle::soft);
    } else if (item == menu_routed) {
        (*canvas_).set_link_style(LinkStyle::routed);
    } else if (item == menu_paper) {
        (*canvas_).set_background(BackgroundVariant::paper);
    } else if (item == menu_blue) {
        (*canvas_).set_background(BackgroundVariant::blue);
    } else if (item == menu_green) {
        (*canvas_).set_background(BackgroundVariant::green);
    } else if (item == menu_grey) {
        (*canvas_).set_background(BackgroundVariant::grey);
    }
}

void GraphRoot::refresh_status() {
    const gf::NodeGraph& graph = document_.graph();
    const CanvasStatistics& statistics = (*canvas_).statistics();
    status_text_.clear();
    append_count(status_text_, live_count_of_nodes(graph));
    status_text_.append(" nodes | ");
    append_count(status_text_, live_count_of_links(graph));
    status_text_.append(" links | zoom ");
    append_decimal(status_text_, (*canvas_).view().zoom);
    status_text_.append(" | ");
    status_text_.append(lod_name(statistics.level));
    status_text_.append(" | selected ");
    append_count(status_text_, (*canvas_).selection().size());
    status_text_.append(" | undo ");
    append_count(status_text_, graph.undo_steps());
    status_text_.append(" | revision ");
    append_count(status_text_, graph.revision());
    if (!document_.path().empty()) {
        status_text_.append(" | ");
        status_text_.append(document_.path().filename().string());
    }
    if ((*status_).text.read() != status_text_) (*status_).text = status_text_;
    ++counters_.status_updates;
}

} // namespace node_graph
