#pragma once

// The avatar atlas (CHAT_STRESS_SPEC section 3.3): generated avatars, a
// symmetric 5 x 5 pattern in the user's hue, kept in a GUI.Forms ImageAtlas
// (gui_forms/resources/image_atlas): 256 slots of 80 x 80 pixels, the least
// recently used replaced by the atlas, each slot drawn under a round clip
// with the initials over it.
//
// This object generates the pixels and keys the slots by user id; the
// ImageAtlas keeps the slots, the backing image, the use order and the
// damage of the controls that drew a slot. A slot is filled when its row
// is measured; a fill made while a window paints is deferred by the atlas
// and written when that paint ends. A row whose slot is not filled yet
// paints a plain disc and is counted.
//
// Ownership: the ImageAtlas is made on first use with the window's
// resources and owned here; it removes its image when this object goes.
// One thread (the window's).

#include "chat_types.hpp"
#include "corpus.hpp"

#include <gui_forms/control.hpp>
#include <gui_forms/resources/image_atlas/image_atlas.hpp>
#include <gui_forms/surface_root/surface_root.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace chat_stress {

// What this object counts beyond ImageAtlasStatistics (they only grow).
struct AvatarCounters final {
    std::uint64_t patterns_drawn{};
    std::uint64_t paint_fallbacks{};
    std::uint64_t atlas_failures{};
};

class AvatarAtlas final {
public:
    static constexpr std::uint32_t slot_count = 256U;
    // 40 logical units at scale 2 (section 3.3).
    static constexpr std::uint32_t slot_pixels = 80U;

    AvatarAtlas();
    ~AvatarAtlas();
    AvatarAtlas(const AvatarAtlas&) = delete;
    AvatarAtlas& operator=(const AvatarAtlas&) = delete;

    // Makes sure `user` has a slot holding its pattern (or a deferred fill
    // of it): acquires the slot and fills it when it was assigned or
    // replaced. Makes the ImageAtlas with `root`'s resources on first use.
    // False when the atlas could not be made or refused the fill.
    bool ensure(gui_forms::SurfaceRoot& root, UserId user, const UserRecord& record);
    // Draws `user`'s slot over `destination` for `consumer` (the chat view),
    // which the atlas repaints when the slot changes. False, drawing
    // nothing and counting a fallback, when the slot is missing or unfilled.
    bool draw(gui_forms::Painter& painter, gui_forms::Control& consumer, UserId user, gui_forms::Rect destination);

    [[nodiscard]] bool created() const noexcept { return atlas_ != nullptr; }
    [[nodiscard]] AvatarCounters counters() const noexcept { return counters_; }
    // The ImageAtlas's statistics; zero before it is made.
    [[nodiscard]] gui_forms::ImageAtlasStatistics statistics() const noexcept;
    // The backing image and the atlas's tables, plus this object's scratch.
    [[nodiscard]] std::size_t storage_bytes() const noexcept;

private:
    // Writes `record`'s pattern into scratch_ (premultiplied BGRA, opaque).
    void draw_pattern(const UserRecord& record);

    std::unique_ptr<gui_forms::ImageAtlas> atlas_{};
    std::vector<std::byte> scratch_{};
    AvatarCounters counters_{};
};

// An opaque colour from a hue in degrees and lightness and saturation in
// percent (HSL), integer arithmetic.
[[nodiscard]] gui_forms::Color hue_colour(std::uint16_t hue, std::uint32_t saturation, std::uint32_t lightness) noexcept;

} // namespace chat_stress
