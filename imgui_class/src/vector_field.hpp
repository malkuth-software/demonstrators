#pragma once

// VectorField: three ValueBars side by side (x, y, z), each scrubbed,
// stepped or typed on its own, as one value. GUI.Forms has no vector
// editor (GAPS.md I3), so the field composes the stock bars and forwards
// their changes.
//
// Events: vector_changed after any bar's value changes (scrub previews
// included), never during set_vector; vector_committed after a bar's
// value_committed. One UI thread.

#include <gui_forms/control.hpp>
#include <gui_forms/controls/value_bar.hpp>
#include <gui_forms/event.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace imgui_class {

struct Vector3 final {
    double x{};
    double y{};
    double z{};

    friend bool operator==(const Vector3& left, const Vector3& right) noexcept {
        return left.x == right.x && left.y == right.y && left.z == right.z;
    }
};

class VectorField final : public gui_forms::Control {
public:
    static constexpr bool initialize_tree_after_construction = true;
    static constexpr double gap = 6.0;

    VectorField(gui_forms::StableId stable_id, double minimum, double maximum);
    void initialize_control_tree();

    // Sets the three values (each clamped to the range), raising nothing.
    void set_vector(Vector3 value);
    [[nodiscard]] Vector3 vector() const;
    [[nodiscard]] gui_forms::ValueBar& bar(std::size_t axis) const { return *bars_.at(axis); }
    [[nodiscard]] gui_forms::Event<const Vector3&>& vector_changed() noexcept { return vector_changed_; }
    [[nodiscard]] gui_forms::Event<const Vector3&>& vector_committed() noexcept { return vector_committed_; }
    [[nodiscard]] std::uint64_t changes() const noexcept { return changes_; }

    [[nodiscard]] gui_forms::Size measure(gui_forms::Size available) override;
    void arrange(gui_forms::Rect final_bounds) override;

private:
    void on_bar_changed(const std::size_t& axis, double value);
    void on_bar_committed(const std::size_t& axis, double value);

    double minimum_{};
    double maximum_{};
    std::array<std::shared_ptr<gui_forms::ValueBar>, 3> bars_{};
    bool setting_{};
    std::uint64_t changes_{};
    gui_forms::Event<const Vector3&> vector_changed_{};
    gui_forms::Event<const Vector3&> vector_committed_{};
};

} // namespace imgui_class
