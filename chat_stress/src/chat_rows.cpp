#include "chat_rows.hpp"

#include <gui_forms/theme/theme/theme.hpp>
#include <gui_forms/types/painter/painter.hpp>

#include <cstdio>
#include <string_view>

namespace chat_stress {

namespace gf = gui_forms;

namespace {

constexpr std::string_view weekday_names[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
constexpr std::string_view edited_mark = "(edited)";
constexpr double header_gap = 8.0;

// Appends a number with at least `digits` digits, zero padded.
void append_padded(std::string& out, std::int64_t value, int digits) {
    char buffer[24]{};
    const int written = std::snprintf(buffer, sizeof(buffer), "%0*lld", digits, static_cast<long long>(value));
    if (written > 0) out.append(buffer, static_cast<std::size_t>(written));
}

// The end of `text`'s first `limit` bytes, moved back to a character start.
std::size_t cut_at_character(std::string_view text, std::size_t limit) noexcept {
    if (text.size() <= limit) return text.size();
    std::size_t cut = limit;
    while (cut > 0U && (static_cast<unsigned char>(text[cut]) & 0xC0U) == 0x80U) --cut;
    return cut;
}

bool same_line_box(const gf::LineBoxMetrics& left, const gf::LineBoxMetrics& right) noexcept {
    const bool same = left.height == right.height && left.baseline == right.baseline;
    return same;
}

gf::Color with_alpha(gf::Color colour, std::uint8_t alpha) noexcept {
    gf::Color result = colour;
    result.alpha = alpha;
    return result;
}

} // namespace

void format_clock(std::int64_t utc_ms, std::string& out) {
    out.clear();
    const CivilTime civil = civil_time_of(utc_ms);
    append_padded(out, civil.hour, 2);
    out.push_back(':');
    append_padded(out, civil.minute, 2);
}

void format_day(std::int64_t utc_ms, std::string& out) {
    out.clear();
    const CivilTime civil = civil_time_of(utc_ms);
    out.append(weekday_names[civil.weekday % 7U]);
    out.push_back(' ');
    append_padded(out, civil.year, 4);
    out.push_back('-');
    append_padded(out, civil.month, 2);
    out.push_back('-');
    append_padded(out, civil.day, 2);
}

ChatRows::ChatRows(Corpus& corpus, AvatarAtlas& atlas)
    : corpus_(corpus), atlas_(atlas), ring_(RowLayoutRing::default_capacity, maximum_overlay_text_bytes),
      wrapper_(maximum_overlay_text_bytes) {
    scratch_.reserve(maximum_overlay_text_bytes + 64U);
}

void ChatRows::attach(gf::VirtualRowsView& view) {
    view_ = &view;
    gf::VirtualRowsCallbacks callbacks{};
    callbacks.measure = gf::Delegate<gf::VirtualRowMeasure&>::bind<ChatRows, &ChatRows::measure>(*this);
    callbacks.paint = gf::Delegate<gf::VirtualRowPaint&>::bind<ChatRows, &ChatRows::paint>(*this);
    callbacks.describe = gf::Delegate<gf::VirtualRowDescription&>::bind<ChatRows, &ChatRows::describe>(*this);
    view.set_row_callbacks(callbacks);
}

void ChatRows::detach() {
    if (view_ == nullptr) return;
    (*view_).set_row_callbacks(gf::VirtualRowsCallbacks{});
    view_ = nullptr;
}

const gf::SurfaceRoot* ChatRows::metrics_root() const noexcept {
    if (view_ == nullptr) return nullptr;
    const gf::SurfaceRoot* root = (*view_).attached_surface_root();
    return root;
}

void ChatRows::refresh_fonts() {
    if (view_ == nullptr) return;
    const gf::Theme& theme = (*view_).effective_theme();
    gf::FontSpec body{};
    body.role = gf::FontRole::content;
    body.size = 14.0;
    gf::FontSpec name{};
    name.role = gf::FontRole::control;
    name.size = 13.0;
    name.weight = 600U;
    gf::FontSpec meta{};
    meta.role = gf::FontRole::control;
    meta.size = 11.0;
    gf::FontSpec initials{};
    initials.role = gf::FontRole::control;
    initials.size = 15.0;
    initials.weight = 600U;
    const gf::FontSpec resolved_body = theme.resolve_font(*view_, body).font;
    const gf::FontSpec resolved_name = theme.resolve_font(*view_, name).font;
    const gf::FontSpec resolved_meta = theme.resolve_font(*view_, meta).font;
    const gf::FontSpec resolved_initials = theme.resolve_font(*view_, initials).font;
    const bool same_fonts = fonts_.generation != 0U && resolved_body == fonts_.body && resolved_name == fonts_.name &&
                            resolved_meta == fonts_.meta && resolved_initials == fonts_.initials;
    if (same_fonts) return;
    const gf::SurfaceRoot* root = metrics_root();
    RowFonts fresh{};
    fresh.body = resolved_body;
    fresh.name = resolved_name;
    fresh.meta = resolved_meta;
    fresh.initials = resolved_initials;
    fresh.body_line = font_line_box(root, resolved_body);
    fresh.name_line = font_line_box(root, resolved_name);
    fresh.meta_line = font_line_box(root, resolved_meta);
    fresh.initials_line = font_line_box(root, resolved_initials);
    const bool same_boxes = same_line_box(fresh.body_line, fonts_.body_line) &&
                            same_line_box(fresh.name_line, fonts_.name_line) &&
                            same_line_box(fresh.meta_line, fonts_.meta_line);
    fresh.generation = fonts_.generation + 1U;
    if (same_boxes && fonts_.generation != 0U) fresh.generation = fonts_.generation;
    if (fresh.generation != fonts_.generation) ++counters_.font_generations;
    fonts_ = fresh;
}

const RowLayoutEntry& ChatRows::layout_row(RowIndex row, double row_width) {
    const MessageId message = corpus_.message_of_row(row);
    MessageId previous = message;
    if (row.value > 0U) {
        const RowIndex before{row.value - 1U};
        previous = corpus_.message_of_row(before);
    }
    const std::uint64_t revision = corpus_.edit_revision(message);
    RowLayoutEntry& entry = ring_.slot(message);
    if (ring_.matches(entry, message, previous, row_width, revision, fonts_.generation)) return entry;

    entry.valid = false;
    entry.message = message;
    entry.previous = previous;
    entry.width = row_width;
    entry.revision = revision;
    entry.font_generation = fonts_.generation;
    corpus_.message_text(message, entry.text);
    entry.author = corpus_.author(message);
    entry.timestamp_ms = corpus_.timestamp_ms(message);
    entry.edited = corpus_.edited(message);
    entry.deleted = corpus_.deleted(message);
    if (row.value == 0U) {
        entry.header = true;
        entry.day_separator = true;
    } else {
        const UserId previous_author = corpus_.author(previous);
        const std::int64_t previous_time = corpus_.timestamp_ms(previous);
        entry.day_separator = utc_day_of(entry.timestamp_ms) != utc_day_of(previous_time);
        const bool continues = previous_author == entry.author &&
                               entry.timestamp_ms - previous_time < RowGeometry::continuation_ms && !entry.day_separator;
        entry.header = !continues;
    }

    const double text_width = RowGeometry::text_width(row_width);
    std::size_t lines = wrapper_.wrap(metrics_root(), entry.text, fonts_.body, text_width, entry.lines);
    if (lines == 0U) lines = 1U;
    double top = RowGeometry::continuation_top;
    if (entry.header) top = RowGeometry::header_top;
    double y = top;
    if (entry.day_separator) y += RowGeometry::day_band;
    if (entry.header) y += fonts_.name_line.height;
    y += static_cast<double>(lines) * fonts_.body_line.height;
    double height = y + RowGeometry::bottom;
    if (entry.header) {
        double avatar_bottom = top + RowGeometry::avatar + RowGeometry::bottom;
        if (entry.day_separator) avatar_bottom += RowGeometry::day_band;
        if (avatar_bottom > height) height = avatar_bottom;
    }
    entry.height = height;
    entry.valid = true;
    return entry;
}

void ChatRows::measure(gf::VirtualRowMeasure& request) {
    if (request.row.value >= corpus_.row_count()) {
        ++counters_.stale_rows_refused;
        return;
    }
    refresh_fonts();
    // The view's width is the content width (viewport less the scroll bar).
    const RowIndex row{request.row.value};
    const RowLayoutEntry& entry = layout_row(row, request.width);
    if (entry.header && view_ != nullptr) {
        gf::SurfaceRoot* root = (*view_).attached_surface_root();
        if (root != nullptr) {
            static_cast<void>(atlas_.ensure(*root, *view_, entry.author, corpus_.user(entry.author)));
        }
    }
    request.height = entry.height;
    ++counters_.rows_measured;
}

void ChatRows::paint_day_separator(gf::Painter& painter, const RowLayoutEntry& entry, gf::Rect bounds, gf::Color rule,
                                   gf::Color text) {
    const double middle = bounds.y + RowGeometry::header_top + RowGeometry::day_band / 2.0;
    format_day(entry.timestamp_ms, scratch_);
    const gf::Size label = painter.measure_text_utf8(scratch_, fonts_.meta);
    const double label_x = bounds.x + (bounds.width - label.width) / 2.0;
    const double gap = 8.0;
    painter.draw_line({bounds.x + RowGeometry::margin_x, middle}, {label_x - gap, middle}, rule, 1.0);
    painter.draw_line({label_x + label.width + gap, middle}, {bounds.x + bounds.width - RowGeometry::margin_x, middle},
                      rule, 1.0);
    const double baseline = middle - fonts_.meta_line.height / 2.0 + fonts_.meta_line.baseline;
    painter.draw_text_utf8({label_x, baseline}, scratch_, fonts_.meta, text);
}

void ChatRows::paint_avatar(gf::Painter& painter, const RowLayoutEntry& entry, gf::Rect avatar) {
    const UserRecord& user = corpus_.user(entry.author);
    const double radius = RowGeometry::avatar / 2.0;
    const std::optional<gf::Rect> source = atlas_.source_of(entry.author);
    if (source.has_value() && atlas_.created()) {
        painter.save();
        painter.clip_rounded_rect(avatar, radius);
        painter.draw_image_region(atlas_.image(), *source, avatar);
        painter.restore();
    } else {
        atlas_.note_paint_fallback();
        painter.fill_rounded_rect(avatar, radius, hue_colour(user.hue, 60U, 46U));
    }
    const std::string_view initials(user.initials);
    const gf::Size size = painter.measure_text_utf8(initials, fonts_.initials);
    const gf::LineBoxMetrics box = fonts_.initials_line;
    const gf::Point origin{avatar.x + (avatar.width - size.width) / 2.0,
                           avatar.y + (avatar.height - box.height) / 2.0 + box.baseline};
    painter.draw_text_utf8(origin, initials, fonts_.initials, gf::Color{255U, 255U, 255U, 255U});
}

void ChatRows::paint(gf::VirtualRowPaint& request) {
    if (request.painter == nullptr || request.view == nullptr) return;
    if (request.row.value >= corpus_.row_count()) {
        ++counters_.stale_rows_refused;
        return;
    }
    refresh_fonts();
    gf::Painter& painter = *request.painter;
    const gf::Rect bounds = request.bounds;
    const RowIndex row{request.row.value};
    const RowLayoutEntry& entry = layout_row(row, bounds.width);
    const gf::BasicControlStyle& style = (*request.view).effective_theme().basic_style();

    if (range_.has_value() && !(row < (*range_).first) && !((*range_).last < row)) {
        painter.fill_rect(bounds, with_alpha(style.accent_light, 160U));
    }

    double top = bounds.y + RowGeometry::continuation_top;
    if (entry.header) top = bounds.y + RowGeometry::header_top;
    if (entry.day_separator) {
        paint_day_separator(painter, entry, bounds, style.border, style.disabled_text);
        top += RowGeometry::day_band;
    }
    // Left to right the avatar is at the left margin and the text follows;
    // mirrored, the avatar is at the right margin and the text starts at the left.
    double text_x = bounds.x + RowGeometry::text_x();
    double avatar_x = bounds.x + RowGeometry::margin_x;
    if (request.mirrored) {
        avatar_x = bounds.x + bounds.width - RowGeometry::margin_x - RowGeometry::avatar;
        text_x = bounds.x + RowGeometry::margin_x;
    }
    double line_top = top;
    if (entry.header) {
        const gf::Rect avatar{avatar_x, top, RowGeometry::avatar, RowGeometry::avatar};
        paint_avatar(painter, entry, avatar);
        const UserRecord& user = corpus_.user(entry.author);
        const double name_baseline = top + fonts_.name_line.baseline;
        painter.draw_text_utf8({text_x, name_baseline}, user.display_name, fonts_.name, hue_colour(user.hue, 60U, 36U));
        const gf::Size name_size = painter.measure_text_utf8(user.display_name, fonts_.name);
        format_clock(entry.timestamp_ms, scratch_);
        if (entry.edited) {
            scratch_.push_back(' ');
            scratch_.append(edited_mark);
        }
        painter.draw_text_utf8({text_x + name_size.width + header_gap, name_baseline}, scratch_, fonts_.meta,
                               style.disabled_text);
        line_top += fonts_.name_line.height;
    }
    gf::Color body = style.text;
    if (entry.deleted) body = style.disabled_text;
    const std::string_view text(entry.text);
    for (const WrappedLine& line : entry.lines) {
        const std::string_view run = text.substr(line.begin, line.end - line.begin);
        painter.draw_text_utf8({text_x, line_top + fonts_.body_line.baseline}, run, fonts_.body, body);
        line_top += fonts_.body_line.height;
        ++counters_.lines_drawn;
    }
    ++counters_.rows_painted;
}

void ChatRows::describe(gf::VirtualRowDescription& request) {
    if (request.row.value >= corpus_.row_count()) {
        ++counters_.stale_rows_refused;
        return;
    }
    const RowIndex row{request.row.value};
    const MessageId message = corpus_.message_of_row(row);
    corpus_.message_text(message, scratch_);
    const UserRecord& user = corpus_.user(corpus_.author(message));
    request.name.append(user.display_name);
    request.name.append(", ");
    const CivilTime civil = civil_time_of(corpus_.timestamp_ms(message));
    append_padded(request.name, civil.hour, 2);
    request.name.push_back(':');
    append_padded(request.name, civil.minute, 2);
    request.name.append(": ");
    const std::size_t cut = cut_at_character(scratch_, accessible_name_text_bytes);
    request.name.append(scratch_, 0U, cut);
    request.value.append(scratch_);
    ++counters_.rows_described;
}

std::size_t ChatRows::storage_bytes() const noexcept {
    const std::size_t bytes = ring_.storage_bytes() + wrapper_.storage_bytes() + scratch_.capacity();
    return bytes;
}

} // namespace chat_stress
