// The node bodies (NODE_GRAPH_SPEC section 4.1): what NodeCanvas leaves to
// the application at full detail, through its paint_body target. NodeCanvas
// draws the node, its caption, its ports, lamps and port labels; this file
// draws, inside the body it is given (and clipped to), a value bar on each
// unconnected input and the kind's live preview. Colours come from the
// canvas look (BodyInk, copied before each paint) except the previews'
// strokes, which carry the data type's colour (socket_colour). Nothing here
// changes the document; it runs inside the canvas's callback scope.

#include "graph_canvas.hpp"

#include <gui_forms/types/painter/painter.hpp>

#include <algorithm>
#include <cstdio>
#include <span>

namespace node_graph {

namespace gf = gui_forms;

namespace {

// Text below this size in view units is not drawn (spec 3).
constexpr double smallest_text = gf::NodeCanvas::smallest_text;
// World units: a value bar's height and the gap kept at the body's right
// edge (clear of the output ports and their lamps).
constexpr double bar_height = 12.0;
constexpr double right_gap = 16.0;
constexpr double preview_inset = 6.0;

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

void format_number(std::string& out, double value) {
    char buffer[32]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%.3g", value);
    out.clear();
    if (written > 0) out.append(buffer, static_cast<std::size_t>(written));
}

} // namespace

void GraphCanvas::read_body_ink() {
    const gf::CanvasLook& look = canvas_look();
    ink_.text = look.port_labels[static_cast<std::size_t>(gf::CanvasPortState::normal)];
    ink_.bar_back = look.headers[static_cast<std::size_t>(gf::CanvasNodeState::normal)].fill;
    ink_.bar_fill = look.selection.stroke;
    ink_.outline = look.nodes[static_cast<std::size_t>(gf::CanvasNodeState::normal)].stroke;
}

void GraphCanvas::paint_body(gf::NodeBodyPaint& request) {
    if (request.painter == nullptr) return;
    const gf::NodeGraph& graph = document_.graph();
    const gf::NodeRecord* node = graph.node(request.node);
    if (node == nullptr) return;
    ++counters_.bodies_painted;
    gf::Painter& painter = *request.painter;
    const std::uint32_t slot = request.node.index;
    const NodeState* state = evaluator_.state(slot);
    paint_value_bars(painter, slot, *node, state, request.body, request.zoom);
    if (state == nullptr || !(*state).kind.has_value() || (*state).evaluation != NodeEvaluation::ok) return;
    paint_preview(painter, slot, *state, request.body, request.zoom);
}

void GraphCanvas::paint_value_bars(gf::Painter& painter, std::uint32_t slot, const gf::NodeRecord& node,
                                   const NodeState* state, gf::Rect body, double zoom) {
    if (state == nullptr) return;
    const gf::NodeGraph& graph = document_.graph();
    const double text_size = 9.0 * zoom;
    const bool numbers = text_size >= smallest_text;
    gf::FontSpec font{};
    font.size = text_size;
    const double left = body.x + body.width * 0.45;
    const double width = std::max(0.0, body.x + body.width - right_gap * zoom - left);
    if (width <= 2.0) return;
    for (std::size_t port = 0U; port < node.ports.size() && port < (*state).inputs.size(); ++port) {
        const gf::PortRecord& record = node.ports[port];
        if (record.direction != gf::PortDirection::input) continue;
        const gf::PortRef ref{gf::NodeId{slot, node.generation}, gf::PortIndex{static_cast<std::uint16_t>(port)}};
        // A connected input shows its source's value in the source's own
        // body; only unconnected inputs scrub in place (spec 4.3).
        if (graph.link(graph.link_into(ref)) != nullptr) continue;
        const gf::WorldPoint anchor_world = gf::port_anchor(node, record.side, record.offset);
        const gf::Point anchor = view().to_view(anchor_world);
        const gf::Rect bar{left, anchor.y - bar_height * zoom / 2.0, width, bar_height * zoom};
        const double value = (*state).inputs[port].number;
        const double share = std::clamp(value, 0.0, 1.0);
        painter.fill_rect(bar, ink_.bar_back);
        painter.fill_rect(gf::Rect{bar.x, bar.y, bar.width * share, bar.height}, with_alpha(ink_.bar_fill, 200));
        ++counters_.value_bars_painted;
        if (!numbers) {
            ++counters_.body_text_skipped;
            continue;
        }
        format_number(number_, value);
        const double baseline = bar.y + bar.height / 2.0 + text_size * 0.35;
        painter.draw_text_utf8(gf::Point{bar.x + 3.0 * zoom, baseline}, number_, font, ink_.text);
    }
}

void GraphCanvas::paint_preview(gf::Painter& painter, std::uint32_t slot, const NodeState& state, gf::Rect body,
                                double zoom) {
    const KindDescriptor& kind = kind_at(*state.kind);
    const double inset = preview_inset * zoom;
    // The preview area: the body's right half, inset, clear of the outputs.
    const gf::Rect area{body.x + body.width * 0.45, body.y + inset,
                        body.width * 0.55 - inset - right_gap * zoom, body.height - 2.0 * inset};
    if (area.width <= 2.0 || area.height <= 2.0) return;
    if (kind.preview == BodyPreview::swatch) {
        Value colour = state.inputs[0];
        for (std::size_t port = 0U; port < kind.port_count; ++port) {
            if (kind.ports[port].direction == gf::PortDirection::output) colour = state.outputs[port];
        }
        const gf::CheckerPattern checker{6.0 * zoom, gf::Color::rgba(200, 200, 200), gf::Color::rgba(150, 150, 150),
                                         gf::Point{area.x, area.y}};
        painter.fill_checker(area, checker);
        painter.fill_rect(area, colour_of(colour));
        painter.stroke_rect(area, ink_.outline, 1.0);
        ++counters_.previews_painted;
        return;
    }
    if (kind.preview == BodyPreview::sparkline) {
        const PlotHistory* history = evaluator_.history(state);
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
        // A sparkline uses the whole body width (the plot kinds have no
        // unconnected inputs once fed).
        const gf::Rect plot{body.x + inset, body.y + inset, body.width - inset - right_gap * zoom,
                            body.height - 2.0 * inset};
        for (std::size_t index = 0U; index < count; ++index) {
            const double sample = (*history).samples[(first + index) % plot_history_samples];
            const double x = plot.x + plot.width * static_cast<double>(index) /
                                          static_cast<double>(plot_history_samples - 1U);
            const double y = plot.y + plot.height * (1.0 - (sample - low) / span);
            preview_points_.push_back(gf::Point{x, y});
        }
        gf::StrokeStyle style{};
        style.color = socket_colour(SocketType::signal);
        style.width = std::clamp(1.5 * zoom, 1.0, 2.5);
        painter.stroke_polyline(std::span<const gf::Point>(preview_points_.data(), preview_points_.size()), style);
        ++counters_.previews_painted;
        return;
    }
    if (kind.preview == BodyPreview::curve) {
        const CurvePoints* points = nullptr;
        if (kind.name == "curve") points = evaluator_.curve(slot);
        if (kind.name == "curve_view") points = evaluator_.curve(state.inputs[0].curve_slot);
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
            const double y = std::clamp(sample_curve_points(*points, t), 0.0, 1.0);
            preview_points_.push_back(gf::Point{area.x + area.width * t, area.y + area.height * (1.0 - y)});
        }
        gf::StrokeStyle style{};
        style.color = socket_colour(SocketType::curve);
        style.width = std::clamp(1.5 * zoom, 1.0, 2.5);
        painter.stroke_rect(area, with_alpha(ink_.outline, 90), 1.0);
        painter.stroke_polyline(std::span<const gf::Point>(preview_points_.data(), preview_points_.size()), style);
        ++counters_.previews_painted;
        return;
    }
    if (kind.preview == BodyPreview::readout) {
        gf::FontSpec large{};
        large.size = 16.0 * zoom;
        if (large.size < smallest_text) {
            ++counters_.body_text_skipped;
            return;
        }
        double value = state.inputs[0].number;
        for (std::size_t port = 0U; port < kind.port_count; ++port) {
            if (kind.ports[port].direction == gf::PortDirection::output) {
                value = state.outputs[port].number;
                break;
            }
        }
        format_number(text_, value);
        painter.draw_text_utf8(gf::Point{area.x, area.y + area.height * 0.5 + large.size * 0.35}, text_, large,
                               ink_.text);
        ++counters_.previews_painted;
    }
}

} // namespace node_graph
