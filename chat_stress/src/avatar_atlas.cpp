#include "avatar_atlas.hpp"

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

AvatarAtlas::AvatarAtlas(std::uint32_t users) {
    slots_.resize(slot_count);
    slot_of_user_.resize(static_cast<std::size_t>(users) + 1U, 0U);
    scratch_.resize(static_cast<std::size_t>(slot_pixels) * slot_pixels * bytes_per_pixel);
}

bool AvatarAtlas::create_image(gf::SurfaceRoot& root) {
    // One transparent image; the zero buffer lives for this call only.
    const std::size_t row_bytes = static_cast<std::size_t>(image_pixels) * bytes_per_pixel;
    std::vector<std::byte> blank(row_bytes * image_pixels, std::byte{0});
    const gf::ImageLoadResult loaded = root.load_bgra32_premultiplied(
        image_pixels, image_pixels, row_bytes, std::span<const std::byte>(blank.data(), blank.size()));
    if (loaded.error != gf::ImageResourceError::none) {
        ++counters_.upload_failures;
        return false;
    }
    image_ = loaded.image;
    created_ = true;
    return true;
}

std::uint32_t AvatarAtlas::least_recent_slot() const noexcept {
    std::uint32_t chosen = 0U;
    for (std::uint32_t slot = 0U; slot < slot_count; ++slot) {
        if (!slots_[slot].used) return slot;
        if (slots_[slot].last_use < slots_[chosen].last_use) chosen = slot;
    }
    return chosen;
}

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
}

bool AvatarAtlas::ensure(gf::SurfaceRoot& root, gf::Control& consumer, UserId user, const UserRecord& record) {
    if (user.value >= slot_of_user_.size()) return false;
    ++clock_;
    const std::uint16_t held = slot_of_user_[user.value];
    if (held != 0U) {
        slots_[held - 1U].last_use = clock_;
        ++counters_.hits;
        return true;
    }
    ++counters_.misses;
    if (!created_ && !create_image(root)) return false;
    const std::uint32_t slot = least_recent_slot();
    Slot& target = slots_[slot];
    if (target.used) {
        slot_of_user_[target.user.value] = 0U;
        ++counters_.evictions;
    }
    draw_pattern(record);
    const std::uint32_t x = (slot % columns) * slot_pixels;
    const std::uint32_t y = (slot / columns) * slot_pixels;
    const gf::ImageLoadResult patched = root.patch_bgra32_premultiplied(
        image_, x, y, slot_pixels, slot_pixels, static_cast<std::uint64_t>(slot_pixels) * bytes_per_pixel,
        std::span<const std::byte>(scratch_.data(), scratch_.size()), consumer, gf::Rect{});
    if (patched.error != gf::ImageResourceError::none) {
        target.used = false;
        ++counters_.upload_failures;
        return false;
    }
    // A patch gives the image a new identity (the registry bumps its
    // generation); the old one is stale from now on.
    image_ = patched.image;
    target.user = user;
    target.used = true;
    target.last_use = clock_;
    slot_of_user_[user.value] = static_cast<std::uint16_t>(slot + 1U);
    ++counters_.uploads;
    return true;
}

std::optional<gf::Rect> AvatarAtlas::source_of(UserId user) {
    if (user.value >= slot_of_user_.size()) return std::nullopt;
    const std::uint16_t held = slot_of_user_[user.value];
    if (held == 0U) return std::nullopt;
    const std::uint32_t slot = held - 1U;
    ++clock_;
    slots_[slot].last_use = clock_;
    const gf::Rect source{static_cast<double>((slot % columns) * slot_pixels),
                          static_cast<double>((slot / columns) * slot_pixels), static_cast<double>(slot_pixels),
                          static_cast<double>(slot_pixels)};
    return source;
}

void AvatarAtlas::release(gf::SurfaceRoot& root) noexcept {
    if (created_) static_cast<void>(root.remove_image(image_));
    created_ = false;
    image_ = gf::ImageId{};
    for (Slot& slot : slots_) slot.used = false;
    for (std::uint16_t& held : slot_of_user_) held = 0U;
}

std::size_t AvatarAtlas::storage_bytes() const noexcept {
    std::size_t bytes = slots_.capacity() * sizeof(Slot) + slot_of_user_.capacity() * sizeof(std::uint16_t) +
                        scratch_.capacity();
    if (created_) bytes += static_cast<std::size_t>(image_pixels) * image_pixels * bytes_per_pixel;
    return bytes;
}

} // namespace chat_stress
