#pragma once

// The avatar atlas (CHAT_STRESS_SPEC section 3.3): generated avatars, a
// symmetric 5 x 5 pattern in the user's hue, drawn into slots of one image
// in the window's image registry, least recently used slot replaced.
// Painted as image regions under a round clip; the initials are drawn as
// text over the image at paint time.
//
// GUI.Forms has no public atlas or sprite-sheet facility (GAPS.md 5), so
// this one is built on SurfaceRoot::load_bgra32_premultiplied (one image)
// and SurfaceRoot::patch_bgra32_premultiplied (one slot at a time). A patch
// takes a consumer control and damages it, so slots are filled while rows
// are measured (layout), never during paint; a paint that finds no slot
// draws a plain disc instead and counts it.
//
// One thread (the window's). The atlas owns its scratch pixels; the image
// belongs to the window's registry and is removed in release().

#include "chat_types.hpp"
#include "corpus.hpp"

#include <gui_forms/control.hpp>
#include <gui_forms/surface_root/surface_root.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace chat_stress {

struct AtlasCounters final {
    std::uint64_t hits{};
    std::uint64_t misses{};
    std::uint64_t evictions{};
    std::uint64_t uploads{};
    std::uint64_t upload_failures{};
    std::uint64_t paint_fallbacks{};
};

class AvatarAtlas final {
public:
    static constexpr std::uint32_t slot_count = 256U;
    static constexpr std::uint32_t columns = 16U;
    // 40 logical units at scale 2 (section 3.3): 1280 x 1280 pixels, 6.5 MB.
    static constexpr std::uint32_t slot_pixels = 80U;
    static constexpr std::uint32_t image_pixels = columns * slot_pixels;

    // `users` is the corpus's user count (user ids 0..users).
    explicit AvatarAtlas(std::uint32_t users);
    AvatarAtlas(const AvatarAtlas&) = delete;
    AvatarAtlas& operator=(const AvatarAtlas&) = delete;

    // Makes sure `user` has a slot, drawing its pattern into the least
    // recently used slot when it has none. `consumer` is the control the
    // patch damages (the chat view). Creates the image on first use.
    // Returns false when the registry refused the image or the patch (the
    // row then paints its fallback disc).
    bool ensure(gui_forms::SurfaceRoot& root, gui_forms::Control& consumer, UserId user, const UserRecord& record);
    // The source rectangle of `user`'s slot in image pixels, marking it
    // used; none when it has no slot.
    [[nodiscard]] std::optional<gui_forms::Rect> source_of(UserId user);
    // Counts a paint that found no slot.
    void note_paint_fallback() noexcept { ++counters_.paint_fallbacks; }

    [[nodiscard]] gui_forms::ImageId image() const noexcept { return image_; }
    [[nodiscard]] bool created() const noexcept { return created_; }
    // Removes the image from `root`'s registry and forgets every slot.
    void release(gui_forms::SurfaceRoot& root) noexcept;

    [[nodiscard]] AtlasCounters counters() const noexcept { return counters_; }
    // The registry image's bytes plus the atlas's own tables.
    [[nodiscard]] std::size_t storage_bytes() const noexcept;

private:
    struct Slot final {
        UserId user{};
        std::uint64_t last_use{};
        bool used{};
    };

    bool create_image(gui_forms::SurfaceRoot& root);
    // Writes `record`'s pattern into scratch_ (premultiplied BGRA, opaque).
    void draw_pattern(const UserRecord& record);
    [[nodiscard]] std::uint32_t least_recent_slot() const noexcept;

    gui_forms::ImageId image_{};
    bool created_{};
    std::uint64_t clock_{};
    std::vector<Slot> slots_{};
    // user id -> slot + 1, zero for none.
    std::vector<std::uint16_t> slot_of_user_{};
    std::vector<std::byte> scratch_{};
    AtlasCounters counters_{};
};

// An opaque colour from a hue in degrees and lightness and saturation in
// percent (HSL), integer arithmetic.
[[nodiscard]] gui_forms::Color hue_colour(std::uint16_t hue, std::uint32_t saturation, std::uint32_t lightness) noexcept;

} // namespace chat_stress
