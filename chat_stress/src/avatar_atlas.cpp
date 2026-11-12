#include "avatar_atlas.hpp"

#include <exception>
#include <span>

namespace chat_stress {

namespace gf = gui_forms;

namespace {

constexpr std::uint32_t pattern_cells = 5U;
constexpr std::uint32_t bytes_per_pixel = 4U;

std::uint8_t channel(std::int32_t value) noexcept {
    std::int32_t clamped = value;
    if (clamped < 0) clamped = 0;
    if (clamped > 255) clamped = 255;
    return static_cast<std::uint8_t>(clamped);
}

void put_pixel(std::vector<std::byte>& pixels, std::uint32_t x, std::uint32_t y, gf::Color colour) noexcept {
    const std::size_t offset = (static_cast<std::size_t>(y) * AvatarAtlas::slot_pixels + x) * bytes_per_pixel;
    // Premultiplied BGRA; the pattern is opaque, so premultiplying changes nothing.
    pixels[offset] = static_cast<std::byte>(colour.blue);
    pixels[offset + 1U] = static_cast<std::byte>(colour.green);
    pixels[offset + 2U] = static_cast<std::byte>(colour.red);
    pixels[offset + 3U] = static_cast<std::byte>(colour.alpha);
}

} // namespace

gf::Color hue_colour(std::uint16_t hue, std::uint32_t saturation, std::uint32_t lightness) noexcept {
    // HSL to RGB in thousandths: chroma = (1 - |2L - 1|) * S.
    const std::int32_t l = static_cast<std::int32_t>(lightness) * 10;          // 0..1000
    const std::int32_t s = static_cast<std::int32_t>(saturation) * 10;         // 0..1000
    std::int32_t two_l_minus_one = 2 * l - 1000;
    if (two_l_minus_one < 0) two_l_minus_one = -two_l_minus_one;
    const std::int32_t chroma = (1000 - two_l_minus_one) * s / 1000;
    const std::int32_t sector = static_cast<std::int32_t>(hue % 360U) / 60;
    const std::int32_t within = static_cast<std::int32_t>(hue % 60U) * 1000 / 60;   // 0..999
    std::int32_t slope = within;
    if (sector % 2 == 1) slope = 1000 - within;
    const std::int32_t x = chroma * slope / 1000;
    const std::int32_t m = l - chroma / 2;
    std::int32_t r = 0;
    std::int32_t g = 0;
    std::int32_t b = 0;
    switch (sector) {
    case 0: r = chroma; g = x; break;
    case 1: r = x; g = chroma; break;
    case 2: g = chroma; b = x; break;
    case 3: g = x; b = chroma; break;
    case 4: r = x; b = chroma; break;
    default: r = chroma; b = x; break;
    }
    const gf::Color colour{channel((r + m) * 255 / 1000), channel((g + m) * 255 / 1000), channel((b + m) * 255 / 1000),
                           255U};
    return colour;
}

AvatarAtlas::AvatarAtlas() {
    scratch_.resize(static_cast<std::size_t>(slot_pixels) * slot_pixels * bytes_per_pixel);
}

AvatarAtlas::~AvatarAtlas() = default;

void AvatarAtlas::draw_pattern(const UserRecord& record) {
    const gf::Color background = hue_colour(record.hue, 55U, 86U);
    const gf::Color foreground = hue_colour(record.hue, 60U, 46U);
    const std::uint32_t cell = slot_pixels / pattern_cells;
    for (std::uint32_t y = 0U; y < slot_pixels; ++y) {
        const std::uint32_t row = y / cell;
        for (std::uint32_t x = 0U; x < slot_pixels; ++x) {
            std::uint32_t column = x / cell;
            // Columns 3 and 4 mirror 1 and 0.
            if (column > 2U) column = pattern_cells - 1U - column;
            const std::uint32_t bit = row * 3U + column;
            const bool on = ((record.pattern >> bit) & 1U) != 0U;
            gf::Color colour = background;
            if (on) colour = foreground;
            put_pixel(scratch_, x, y, colour);
        }
    }
    ++counters_.patterns_drawn;
}

bool AvatarAtlas::ensure(gf::SurfaceRoot& root, UserId user, const UserRecord& record) {
    if (atlas_ == nullptr) {
        gf::ImageAtlasLayout layout{};
        layout.slot_width = slot_pixels;
        layout.slot_height = slot_pixels;
        layout.slot_count = slot_count;
        layout.gutter = 1U;
        // The chat view is the only control drawing avatars.
        layout.users_per_slot = 1U;
        layout.replacement = gf::ImageAtlasReplacement::least_recently_used;
        layout.pending_fills = gf::image_atlas_pending_every_slot;
        try {
            atlas_ = std::make_unique<gf::ImageAtlas>(root.resources(), layout);
        } catch (const std::exception&) {
            // Gone resources, another thread or the registry's quota: rows
            // paint their fallback disc.
            ++counters_.atlas_failures;
            return false;
        }
    }
    const std::uint64_t key = user.value;
    const gf::ImageAtlasAcquire acquired = (*atlas_).acquire(key);
    if (acquired.outcome == gf::ImageAtlasAcquireOutcome::full) {
        ++counters_.atlas_failures;
        return false;
    }
    // A hit whose earlier fill was refused is filled again.
    const bool needs_fill = acquired.outcome != gf::ImageAtlasAcquireOutcome::hit || !(*atlas_).filled(acquired.slot);
    if (!needs_fill) return true;
    draw_pattern(record);
    const gf::ImageAtlasError error =
        (*atlas_).fill(acquired.slot, static_cast<std::uint64_t>(slot_pixels) * bytes_per_pixel,
                       std::span<const std::byte>(scratch_.data(), scratch_.size()));
    const bool accepted = error == gf::ImageAtlasError::none || error == gf::ImageAtlasError::deferred;
    if (!accepted) ++counters_.atlas_failures;
    return accepted;
}

bool AvatarAtlas::draw(gf::Painter& painter, gf::Control& consumer, UserId user, gf::Rect destination) {
    if (atlas_ == nullptr) {
        ++counters_.paint_fallbacks;
        return false;
    }
    const gf::ImageAtlasSlot slot = (*atlas_).find(user.value);
    const bool drawn = static_cast<bool>(slot) && (*atlas_).draw(painter, consumer, slot, destination);
    if (!drawn) ++counters_.paint_fallbacks;
    return drawn;
}

gf::ImageAtlasStatistics AvatarAtlas::statistics() const noexcept {
    if (atlas_ == nullptr) return gf::ImageAtlasStatistics{};
    return (*atlas_).statistics();
}

std::size_t AvatarAtlas::storage_bytes() const noexcept {
    std::size_t bytes = scratch_.capacity();
    if (atlas_ != nullptr) {
        const gf::ImageAtlasStatistics statistics = (*atlas_).statistics();
        bytes += static_cast<std::size_t>(statistics.image_bytes + statistics.table_bytes);
    }
    return bytes;
}

} // namespace chat_stress
