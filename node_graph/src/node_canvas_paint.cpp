// The canvas's painting (NODE_GRAPH_SPEC sections 3 and 4): background and
// world grid, links, nodes by level of detail, ports and lamps in batches,
// live previews, and the gesture overlay. Everything is drawn in view units
// through the Painter vocabulary; nothing here changes the document.

#include "node_canvas_view.hpp"

#include <gui_forms/types/painter/painter.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>

namespace node_graph {

namespace gf = gui_forms;

namespace {

constexpr double caption_height = 24.0;
// Text below this size in view units is not shaped (spec 3, LOD rule).
constexpr double smallest_text = 6.0;

struct CanvasPalette final {
    gf::Color background{};
    gf::Color minor{};
    gf::Color major{};
    gf::Color body{};
    gf::Color border{};
    gf::Color text{};
    gf::Color caption_text{};
    gf::Color selection{};
    gf::Color bar_back{};
    gf::Color bar_fill{};
};

CanvasPalette palette_of(BackgroundVariant variant) noexcept {
    CanvasPalette palette{};
    palette.caption_text = gf::Color::rgba(250, 250, 250);
    palette.selection = gf::Color::rgba(255, 190, 60);
    palette.bar_fill = gf::Color::rgba(96, 150, 220);
    switch (variant) {
    case BackgroundVariant::paper:
        palette.background = gf::Color::rgba(246, 244, 238);
        palette.minor = gf::Color::rgba(214, 220, 228);
        palette.major = gf::Color::rgba(176, 188, 204);
        palette.body = gf::Color::rgba(255, 255, 255);
        palette.border = gf::Color::rgba(120, 130, 140);
        palette.text = gf::Color::rgba(30, 34, 40);
        palette.bar_back = gf::Color::rgba(226, 230, 236);
        return palette;
    case BackgroundVariant::blue:
        palette.background = gf::Color::rgba(28, 58, 94);
        palette.minor = gf::Color::rgba(52, 86, 124);
        palette.major = gf::Color::rgba(86, 124, 166);
        palette.body = gf::Color::rgba(36, 44, 56);
        palette.border = gf::Color::rgba(140, 170, 200);
        palette.text = gf::Color::rgba(226, 232, 240);
        palette.bar_back = gf::Color::rgba(54, 64, 78);
        return palette;
    case BackgroundVariant::green:
        palette.background = gf::Color::rgba(28, 64, 44);
        palette.minor = gf::Color::rgba(48, 90, 64);
        palette.major = gf::Color::rgba(80, 126, 96);
        palette.body = gf::Color::rgba(36, 48, 40);
        palette.border = gf::Color::rgba(150, 190, 160);
        palette.text = gf::Color::rgba(226, 236, 228);
        palette.bar_back = gf::Color::rgba(54, 70, 60);
        return palette;
    case BackgroundVariant::grey:
        palette.background = gf::Color::rgba(43, 43, 46);
        palette.minor = gf::Color::rgba(58, 58, 62);
        palette.major = gf::Color::rgba(80, 80, 86);
        palette.body = gf::Color::rgba(52, 52, 58);
        palette.border = gf::Color::rgba(130, 130, 138);
        palette.text = gf::Color::rgba(226, 226, 230);
        palette.bar_back = gf::Color::rgba(66, 66, 72);
        return palette;
    }
    return palette_of(BackgroundVariant::blue);
}

gf::Color family_colour(KindFamily family) noexcept {
    switch (family) {
    case KindFamily::source: return gf::Color::rgba(70, 120, 190);
    case KindFamily::math: return gf::Color::rgba(110, 110, 130);
    case KindFamily::colour: return gf::Color::rgba(180, 120, 50);
    case KindFamily::curve: return gf::Color::rgba(60, 150, 160);
    case KindFamily::sink: return gf::Color::rgba(150, 80, 140);
    case KindFamily::structure: return gf::Color::rgba(100, 100, 100);
    }
    return gf::Color::rgba(110, 110, 130);
}

gf::Color with_alpha(gf::Color colour, std::uint8_t alpha) noexcept {
    colour.alpha = alpha;
    return colour;
}

gf::Color colour_of(const Value& value) noexcept {
    const double red = std::clamp(value.colour[0], 0.0, 1.0) * 255.0;
    const double green = std::clamp(value.colour[1], 0.0, 1.0) * 255.0;
    const double blue = std::clamp(value.colour[2], 0.0, 1.0) * 255.0;
    const gf::Color colour = gf::Color::rgba(static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green),
                                             static_cast<std::uint8_t>(blue));
    return colour;
}

// FNV-1a over the title's bytes: the title cache's key.
std::uint64_t text_hash(std::string_view text) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char byte : text) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool rects_overlap(const gf::Rect& left, const gf::Rect& right) noexcept {
    const bool apart = left.x > right.x + right.width || right.x > left.x + left.width || left.y > right.y + right.height ||
                       right.y > left.y + left.height;
    return !apart;
}

void format_number(std::string& out, double value) {
    char buffer[32]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%.3g", value);
    out.clear();
    if (written > 0) out.append(buffer, static_cast<std::size_t>(written));
}

} // namespace

void NodeCanvasView::on_paint(gf::Painter& painter, gf::Rect local_damage) {
    static_cast<void>(local_damage);
    ++statistics_.frames_painted;
    const gf::NodeGraph& graph = document_.graph();
    const gf::Rect viewport = client_rectangle();
    statistics_.level = lod_.update(view_.zoom);
    statistics_.level_transitions = lod_.transitions();
    if (link_style_ == LinkStyle::routed && router_.routed_revision() != graph.revision()) {
        const gf::LinkRouteStatistics routed = router_.route(graph, router_options_);
        ++statistics_.router_runs;
        statistics_.router_expanded += routed.expanded;
        statistics_.router_storage_growths += routed.storage_growths;
        statistics_.router_fallbacks += routed.fallbacks;
        statistics_.last_route = routed;
    }
    ensure_index();
    index_.query(graph, view_.rect_to_world(viewport), false, visible_);
    // Slot order is paint order (later slots on top), as hit testing assumes.
    std::sort(visible_.begin(), visible_.end());
    if (titles_.size() < graph.node_slots().size()) titles_.resize(graph.node_slots().size());

    // The opaque fill comes first, untranslated and unclipped, so that its
    // cover can be reported (Control::report_opaque_cover).
    painter.fill_rect(viewport, palette_of(background_).background);
    report_opaque_cover(painter, viewport);
    painter.save();
    painter.clip_rect(viewport);
    paint_background(painter, viewport);
    paint_links(painter, viewport);
    paint_nodes(painter);
    paint_ports(painter);
    paint_overlay(painter);
    painter.restore();
    statistics_.last_frame_nodes = visible_.size();
    statistics_.last_frame_live_sinks = live_sink_rects_.size();
    enlist_live_sinks();
}

void NodeCanvasView::paint_background(gf::Painter& painter, gf::Rect viewport) {
    const CanvasPalette palette = palette_of(background_);
    // One level of the s * 4^k grid, faded across its band (the Painter's
    // grid levels: GAPS.md, spec gap 1 resolved).
    const gf::GridLevel level = gf::select_grid_level(grid_spacing, view_.zoom, gf::GridLevelBand{});
    const gf::Point origin = view_.to_view(gf::WorldPoint{0.0, 0.0});
    const gf::GridPattern pattern = gf::grid_pattern_at_level(level, origin, palette.minor, palette.major, 1.0);
    painter.fill_grid_pattern(viewport, pattern);
}

void NodeCanvasView::paint_links(gf::Painter& painter, gf::Rect viewport) {
    const gf::NodeGraph& graph = document_.graph();
    const CanvasPalette palette = palette_of(background_);
    const std::span<const gf::LinkRecord> links = graph.link_slots();
    const std::size_t segments = curve_segments();
    const double width = std::clamp(2.0 * view_.zoom, 1.0, 3.0);
    std::size_t painted = 0U;
    for (std::size_t slot = 0U; slot < links.size(); ++slot) {
        const gf::LinkRecord& link = links[slot];
        if (!link.live) continue;
        const gf::PortRecord* from_port = graph.port(link.from);
        const gf::PortRecord* to_port = graph.port(link.to);
        if (from_port == nullptr || to_port == nullptr) continue;
        if (link_style_ == LinkStyle::soft) {
            const std::optional<gf::Point> from = port_view_anchor(link.from);
            const std::optional<gf::Point> to = port_view_anchor(link.to);
            if (!from.has_value() || !to.has_value()) continue;
            const gf::Rect box = curve_bounds(soft_link_curve(*from, *to, view_.zoom));
            if (!rects_overlap(box, viewport)) {
                ++statistics_.links_culled;
                continue;
            }
        }
        const std::size_t count = link_path(static_cast<std::uint32_t>(slot), link, segments);
        if (count < 2U) continue;
        statistics_.link_points_flattened += count;
        const std::span<const gf::Point> path(link_points_.data(), count);
        const std::optional<SocketType> type = socket_type_from_number((*from_port).socket_type);
        gf::StrokeStyle style{};
        style.color = socket_colour(type.has_value() ? *type : SocketType::float_value);
        style.width = width;
        style.join = gf::StrokeJoin::round;
        style.cap = gf::StrokeCap::round;
        // A lossy conversion is drawn dashed (spec 4.1: lossy marked).
        if (conversion_between_numbers((*from_port).socket_type, (*to_port).socket_type) == Conversion::lossy) {
            style.dashes[0] = 6.0 * view_.zoom;
            style.dashes[1] = 4.0 * view_.zoom;
            style.dash_count = 2U;
        }
        const gf::LinkId id{static_cast<std::uint32_t>(slot), link.generation};
        if (selected_link_.has_value() && *selected_link_ == id) {
            gf::StrokeStyle halo = style;
            halo.color = with_alpha(palette.selection, 160);
            halo.width = width + 4.0;
            halo.dash_count = 0U;
            painter.stroke_polyline(path, halo);
        }
        painter.stroke_polyline(path, style);
        ++painted;
    }
    statistics_.links_painted += painted;
    statistics_.last_frame_links = painted;
}

void NodeCanvasView::paint_nodes(gf::Painter& painter) {
    for (std::vector<gf::Point>& centres : port_centres_) centres.clear();
    for (std::vector<gf::Point>& centres : dimmed_centres_) centres.clear();
    for (std::vector<gf::Point>& centres : lamp_centres_) centres.clear();
    live_sink_rects_.clear();
    const std::span<const gf::NodeRecord> slots = document_.graph().node_slots();
    for (const std::uint32_t slot : visible_) {
        if (slot >= slots.size() || !slots[slot].live) continue;
        paint_node(painter, slot, slots[slot]);
    }
    statistics_.nodes_painted += visible_.size();
}

void NodeCanvasView::paint_node(gf::Painter& painter, std::uint32_t slot, const gf::NodeRecord& node) {
    const CanvasPalette palette = palette_of(background_);
    const gf::Rect rect = view_.rect_to_view(node.bounds);
    const std::optional<KindIndex> kind = find_kind(node.kind);
    const KindFamily family = kind.has_value() ? kind_at(*kind).family : KindFamily::structure;
    const gf::Color band = family_colour(family);
    const bool is_selected = slot < selected_flags_.size() && selected_flags_[slot] != 0U;
    const gf::Color border = is_selected ? palette.selection : palette.border;
    const double border_width = is_selected ? 2.0 : 1.0;
    const LevelOfDetail level = lod_.level();
    if (level == LevelOfDetail::overview) {
        painter.fill_rect(rect, is_selected ? palette.selection : band);
        return;
    }
    const double radius = 6.0 * view_.zoom;
    painter.fill_rounded_rect(rect, radius, palette.body);
    if (level == LevelOfDetail::silhouette) {
        painter.stroke_rounded_rect(rect, radius, border, border_width);
        return;
    }
    const double caption = std::min(caption_height * view_.zoom, rect.height);
    const gf::Rect caption_rect{rect.x, rect.y, rect.width, caption};
    painter.fill_rounded_rect_corners(caption_rect, gf::PaintCornerRadii{radius, radius, 0.0, 0.0}, band);
    painter.stroke_rounded_rect(rect, radius, border, border_width);
    paint_title(painter, slot, node, caption_rect, 12.0 * view_.zoom);

    // Ports (both compact and full) into the batches; lamps for outputs.
    const NodeState* state = evaluator_.state(slot);
    const gf::PortRecord* drag_source = gesture_ == Gesture::link ? document_.graph().port(link_from_) : nullptr;
    for (std::size_t port = 0U; port < node.ports.size(); ++port) {
        const gf::PortRecord& record = node.ports[port];
        const gf::Point anchor = view_.to_view(gf::port_anchor(node, record.side, record.offset));
        const std::optional<SocketType> type = socket_type_from_number(record.socket_type);
        const std::size_t batch = static_cast<std::size_t>(type.has_value() ? *type : SocketType::float_value);
        bool dim = false;
        if (drag_source != nullptr) {
            // During a link drag, ports that cannot take it dim (spec 4.2).
            const bool candidate = record.direction == gf::PortDirection::input && slot != link_from_.node.index &&
                                   connectable((*drag_source).socket_type, record.socket_type);
            dim = !candidate;
        }
        if (dim) {
            dimmed_centres_[batch].push_back(anchor);
        } else {
            port_centres_[batch].push_back(anchor);
        }
        if (record.direction == gf::PortDirection::output) {
            const std::size_t lamp = static_cast<std::size_t>(record.lamp);
            if (lamp < lamp_centres_.size()) lamp_centres_[lamp].push_back(gf::Point{anchor.x - 10.0 * view_.zoom, anchor.y});
        }
    }
    if (state != nullptr && (*state).kind.has_value()) {
        const KindDescriptor& descriptor = kind_at(*(*state).kind);
        if ((*state).time_dependent && descriptor.family == KindFamily::sink) {
            live_sink_rects_.push_back(gf::Rect{rect.x - 2.0, rect.y - 2.0, rect.width + 4.0, rect.height + 4.0});
        }
    }
    if (level != LevelOfDetail::full) return;
    const gf::Rect body{rect.x, rect.y + caption, rect.width, rect.height - caption};
    paint_body(painter, slot, node, body);
}

void NodeCanvasView::paint_title(gf::Painter& painter, std::uint32_t slot, const gf::NodeRecord& node,
                                 gf::Rect caption, double font_size) {
    if (font_size < smallest_text) {
        ++statistics_.text_skipped_small;
        return;
    }
    const CanvasPalette palette = palette_of(background_);
    gf::FontSpec font{};
    font.size = font_size;
    font.weight = 600U;
    if (line_box_font_size_ != font_size) {
        line_box_ = gf::paragraph_line_box(&painter, font, std::nullopt);
        line_box_font_size_ = font_size;
    }
    gf::ParagraphStyle style{};
    style.font = font;
    style.line_box = line_box_;
    style.width = std::max(0.0, caption.width - 12.0 * view_.zoom);
    style.white_space = gf::ParagraphWhiteSpace::preserve_line_ends;
    style.overflow_wrap = gf::ParagraphOverflowWrap::break_word;
    style.fit = gf::ParagraphFit::verified;
    TitleLayout& cached = titles_[slot];
    const std::uint64_t hash = text_hash(node.title);
    const bool hit = cached.valid && cached.generation == node.generation && cached.title_hash == hash &&
                     cached.font_size == font_size && cached.width == style.width;
    if (hit) {
        ++statistics_.title_cache_hits;
    } else {
        cached.valid = false;
        const gf::ParagraphStorageRequirement bound =
            gf::paragraph_storage_bound(node.title, gf::ParagraphWhiteSpace::preserve_line_ends);
        paragraph_.reserve(bound);
        gf::ParagraphLayoutResult result = gf::lay_out_paragraph(&painter, node.title, style, paragraph_.storage());
        if (result.required_storage() != nullptr) {
            paragraph_.reserve(*result.required_storage());
            result = gf::lay_out_paragraph(&painter, node.title, style, paragraph_.storage());
        }
        const gf::ParagraphMetrics* metrics = result.metrics();
        if (metrics == nullptr) return;
        ++statistics_.titles_laid_out;
        const std::span<const gf::ParagraphLine> lines = paragraph_.lines((*metrics).line_count);
        cached.line_count = std::min<std::size_t>(lines.size(), cached.lines.size());
        for (std::size_t index = 0U; index < cached.line_count; ++index) cached.lines[index] = lines[index];
        cached.generation = node.generation;
        cached.title_hash = hash;
        cached.font_size = font_size;
        cached.width = style.width;
        cached.valid = true;
    }
    if (cached.line_count == 0U) return;
    // The caption holds one line; a wrapped title shows its first.
    gf::ParagraphPaint paint{};
    paint.origin = gf::Point{caption.x + 6.0 * view_.zoom, caption.y + (caption.height - style.line_box.height) / 2.0};
    paint.color = palette.caption_text;
    paint.device_scale = 1.0;
    paint.visible = caption;
    const std::span<const gf::ParagraphLine> first(cached.lines.data(), 1U);
    statistics_.title_lines_drawn += gf::paint_paragraph(painter, node.title, first, style, paint);
}

void NodeCanvasView::paint_body(gf::Painter& painter, std::uint32_t slot, const gf::NodeRecord& node, gf::Rect body) {
    const CanvasPalette palette = palette_of(background_);
    const double label_size = 10.0 * view_.zoom;
    const NodeState* state = evaluator_.state(slot);
    gf::FontSpec font{};
    font.size = label_size;
    const bool labels = label_size >= smallest_text;
    if (!labels) ++statistics_.text_skipped_small;
    const gf::NodeGraph& graph = document_.graph();
    // Port rows: names, and for unconnected inputs a value bar (spec 4.1).
    for (std::size_t port = 0U; port < node.ports.size() && labels; ++port) {
        const gf::PortRecord& record = node.ports[port];
        const gf::Point anchor = view_.to_view(gf::port_anchor(node, record.side, record.offset));
        const double baseline = anchor.y + label_size * 0.35;
        if (record.direction == gf::PortDirection::output) {
            const gf::Size size = painter.measure_text_utf8(record.name, font);
            painter.draw_text_utf8(gf::Point{anchor.x - 16.0 * view_.zoom - size.width, baseline}, record.name, font,
                                   palette.text);
            ++statistics_.text_runs_drawn;
            continue;
        }
        const gf::PortRef ref{gf::NodeId{slot, node.generation}, gf::PortIndex{static_cast<std::uint16_t>(port)}};
        const bool connected = graph.link(graph.link_into(ref)) != nullptr;
        text_.assign(record.name);
        if (!connected && state != nullptr && port < (*state).inputs.size()) {
            const double value = (*state).inputs[port].number;
            const gf::Rect bar{anchor.x + 8.0 * view_.zoom, anchor.y - 7.0 * view_.zoom, body.width * 0.55, 14.0 * view_.zoom};
            painter.fill_rect(bar, palette.bar_back);
            const double share = std::clamp(value, 0.0, 1.0);
            painter.fill_rect(gf::Rect{bar.x, bar.y, bar.width * share, bar.height}, palette.bar_fill);
            std::string number{};
            format_number(number, value);
            text_.append(" ");
            text_.append(number);
        }
        painter.draw_text_utf8(gf::Point{anchor.x + 11.0 * view_.zoom, baseline}, text_, font, palette.text);
        ++statistics_.text_runs_drawn;
    }
    if (state == nullptr || !(*state).kind.has_value() || (*state).evaluation != NodeEvaluation::ok) return;
    const KindDescriptor& kind = kind_at(*(*state).kind);
    // The preview area: the body's right half, inset.
    const double inset = 6.0 * view_.zoom;
    const gf::Rect area{body.x + body.width * 0.45, body.y + inset, body.width * 0.55 - 2.0 * inset - 14.0 * view_.zoom,
                        body.height - 2.0 * inset};
    if (area.width <= 2.0 || area.height <= 2.0) return;
    if (kind.preview == BodyPreview::swatch) {
        Value colour = (*state).inputs[0];
        for (std::size_t port = 0U; port < kind.port_count; ++port) {
            if (kind.ports[port].direction == gf::PortDirection::output) colour = (*state).outputs[port];
        }
        const gf::CheckerPattern checker{6.0 * view_.zoom, gf::Color::rgba(200, 200, 200), gf::Color::rgba(150, 150, 150),
                                         gf::Point{area.x, area.y}};
        painter.fill_checker(area, checker);
        painter.fill_rect(area, colour_of(colour));
        painter.stroke_rect(area, palette.border, 1.0);
        return;
    }
    if (kind.preview == BodyPreview::sparkline) {
        const PlotHistory* history = evaluator_.history(*state);
        if (history == nullptr || (*history).count < 2U) return;
        double low = (*history).samples[0];
        double high = low;
        for (std::size_t index = 0U; index < (*history).count; ++index) {
            low = std::min(low, (*history).samples[index]);
            high = std::max(high, (*history).samples[index]);
        }
        const double span = high - low > 0.0 ? high - low : 1.0;
        preview_points_.clear();
        const std::size_t count = (*history).count;
        const std::size_t first = ((*history).head + plot_history_samples - count) % plot_history_samples;
        const gf::Rect plot{body.x + inset, body.y + inset, body.width - 2.0 * inset - 14.0 * view_.zoom,
                            body.height - 2.0 * inset};
        for (std::size_t index = 0U; index < count; ++index) {
            const double sample = (*history).samples[(first + index) % plot_history_samples];
            const double x = plot.x + plot.width * static_cast<double>(index) / static_cast<double>(plot_history_samples - 1U);
            const double y = plot.y + plot.height * (1.0 - (sample - low) / span);
            preview_points_.push_back(gf::Point{x, y});
        }
        gf::StrokeStyle style{};
        style.color = socket_colour(SocketType::signal);
        style.width = std::clamp(1.5 * view_.zoom, 1.0, 2.5);
        painter.stroke_polyline(std::span<const gf::Point>(preview_points_.data(), preview_points_.size()), style);
        return;
    }
    if (kind.preview == BodyPreview::curve) {
        const CurvePoints* points = nullptr;
        if (kind.name == "curve") points = evaluator_.curve(slot);
        if (kind.name == "curve_view") points = evaluator_.curve((*state).inputs[0].curve_slot);
        CurvePoints envelope{};
        if (kind.name == "envelope") {
            // The envelope's shape over one cycle (attack, decay, sustain,
            // release), drawn at fixed proportions rather than from its
            // properties.
            envelope.count = 5U;
            envelope.x = {0.0, 0.1, 0.3, 0.8, 1.0};
            envelope.y = {0.0, 1.0, 0.6, 0.6, 0.0};
            points = &envelope;
        }
        if (points == nullptr || (*points).count == 0U) return;
        preview_points_.clear();
        constexpr std::size_t samples = 32U;
        for (std::size_t index = 0U; index <= samples; ++index) {
            const double t = static_cast<double>(index) / static_cast<double>(samples);
            const double y = sample_curve_points(*points, t);
            preview_points_.push_back(gf::Point{area.x + area.width * t, area.y + area.height * (1.0 - std::clamp(y, 0.0, 1.0))});
        }
        gf::StrokeStyle style{};
        style.color = socket_colour(SocketType::curve);
        style.width = std::clamp(1.5 * view_.zoom, 1.0, 2.5);
        painter.stroke_rect(area, with_alpha(palette.border, 90), 1.0);
        painter.stroke_polyline(std::span<const gf::Point>(preview_points_.data(), preview_points_.size()), style);
        return;
    }
    if (kind.preview == BodyPreview::readout && labels) {
        double value = (*state).inputs[0].number;
        for (std::size_t port = 0U; port < kind.port_count; ++port) {
            if (kind.ports[port].direction == gf::PortDirection::output) {
                value = (*state).outputs[port].number;
                break;
            }
        }
        format_number(text_, value);
        gf::FontSpec large{};
        large.size = 16.0 * view_.zoom;
        painter.draw_text_utf8(gf::Point{area.x, area.y + area.height * 0.5 + large.size * 0.35}, text_, large, palette.text);
        ++statistics_.text_runs_drawn;
    }
}

void NodeCanvasView::paint_ports(gf::Painter& painter) {
    const double size = std::max(3.0, 9.0 * view_.zoom);
    for (std::size_t type = 0U; type < socket_type_count; ++type) {
        const SocketType socket = static_cast<SocketType>(type);
        gf::MarkerStyle style{};
        style.shape = socket_glyph(socket);
        style.size = size;
        style.fill = socket_colour(socket);
        style.edge = gf::Color::rgba(20, 20, 24);
        style.edge_width = 1.0;
        if (!port_centres_[type].empty()) {
            painter.draw_markers(std::span<const gf::Point>(port_centres_[type].data(), port_centres_[type].size()), style);
        }
        if (!dimmed_centres_[type].empty()) {
            style.fill = with_alpha(style.fill, 60);
            style.edge = with_alpha(style.edge, 60);
            painter.draw_markers(std::span<const gf::Point>(dimmed_centres_[type].data(), dimmed_centres_[type].size()),
                                 style);
        }
    }
    // Lamps: off, on (green), error (red), the owner's indicator lights.
    const std::array<gf::Color, 3> lamp_colours{gf::Color::rgba(90, 90, 96), gf::Color::rgba(80, 200, 90),
                                                gf::Color::rgba(220, 60, 50)};
    for (std::size_t lamp = 0U; lamp < lamp_centres_.size(); ++lamp) {
        if (lamp_centres_[lamp].empty()) continue;
        gf::MarkerStyle style{};
        style.shape = gf::MarkerShape::circle;
        style.size = std::max(2.0, 5.0 * view_.zoom);
        style.fill = lamp_colours[lamp];
        style.edge = gf::Color::rgba(0, 0, 0, 0);
        style.edge_width = 0.0;
        painter.draw_markers(std::span<const gf::Point>(lamp_centres_[lamp].data(), lamp_centres_[lamp].size()), style);
    }
}

void NodeCanvasView::paint_overlay(gf::Painter& painter) {
    const CanvasPalette palette = palette_of(background_);
    if (!gesture_planned_) return;
    if (gesture_ == Gesture::marquee) {
        const gf::Rect rect{std::min(press_view_.x, current_view_.x), std::min(press_view_.y, current_view_.y),
                            std::fabs(current_view_.x - press_view_.x), std::fabs(current_view_.y - press_view_.y)};
        painter.fill_rect(rect, with_alpha(palette.selection, 40));
        painter.stroke_rect(rect, palette.selection, 1.0);
        return;
    }
    if (gesture_ == Gesture::knife) {
        painter.draw_line(press_view_, current_view_, gf::Color::rgba(230, 70, 60), 2.0);
        return;
    }
    if (gesture_ != Gesture::link) return;
    const std::optional<gf::Point> from = port_view_anchor(link_from_);
    if (!from.has_value()) return;
    gf::Point end = current_view_;
    if (link_hit_.has_value() && *link_hit_ < candidates_.size()) end = candidates_[*link_hit_].view_anchor;
    const CubicCurve curve = soft_link_curve(*from, end, view_.zoom);
    const std::size_t count = flatten_curve(curve, 24U, std::span<gf::Point>(link_points_.data(), link_points_.size()));
    gf::StrokeStyle style{};
    style.color = palette.selection;
    style.width = 2.0;
    style.cap = gf::StrokeCap::round;
    painter.stroke_polyline(std::span<const gf::Point>(link_points_.data(), count), style);
    if (link_hit_.has_value()) {
        gf::MarkerStyle ring{};
        ring.shape = gf::MarkerShape::circle;
        ring.size = 16.0;
        ring.fill = gf::Color::rgba(0, 0, 0, 0);
        ring.edge = palette.selection;
        ring.edge_width = 2.0;
        const std::array<gf::Point, 1> centre{end};
        painter.draw_markers(std::span<const gf::Point>(centre.data(), centre.size()), ring);
    }
}

} // namespace node_graph
