#include "vector_field.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace imgui_class {

namespace gf = gui_forms;

namespace {

constexpr std::array<std::string_view, 3> axis_names{"x", "y", "z"};

} // namespace

VectorField::VectorField(gf::StableId stable_id, double minimum, double maximum)
    : gf::Control(std::move(stable_id)), minimum_(minimum), maximum_(maximum) {}

void VectorField::initialize_control_tree() {
    const std::string base(stable_id().value());
    for (std::size_t axis = 0U; axis < bars_.size(); ++axis) {
        std::shared_ptr<gf::ValueBar> bar = gf::make_control<gf::ValueBar>(gf::StableId(base + "." + std::string(axis_names[axis])));
        (*bar).set_range(minimum_, maximum_);
        (*bar).label = std::string(axis_names[axis]);
        (*bar).precision = 3;
        (*bar).default_value = 0.0;
        (*bar).accessible_name = std::string(axis_names[axis]);
        add_child(bar);
        gf::on((*bar).value_changed(), *this, &VectorField::on_bar_changed, axis);
        gf::on((*bar).value_committed(), *this, &VectorField::on_bar_committed, axis);
        bars_[axis] = std::move(bar);
    }
}

void VectorField::set_vector(Vector3 value) {
    setting_ = true;
    (*bars_[0]).value = std::clamp(value.x, minimum_, maximum_);
    (*bars_[1]).value = std::clamp(value.y, minimum_, maximum_);
    (*bars_[2]).value = std::clamp(value.z, minimum_, maximum_);
    setting_ = false;
}

Vector3 VectorField::vector() const {
    Vector3 value{};
    value.x = (*bars_[0]).value.read();
    value.y = (*bars_[1]).value.read();
    value.z = (*bars_[2]).value.read();
    return value;
}

void VectorField::on_bar_changed(const std::size_t& axis, double value) {
    static_cast<void>(axis);
    static_cast<void>(value);
    if (setting_) return;
    ++changes_;
    const Vector3 current = vector();
    vector_changed_.emit(current);
}

void VectorField::on_bar_committed(const std::size_t& axis, double value) {
    static_cast<void>(axis);
    static_cast<void>(value);
    const Vector3 current = vector();
    vector_committed_.emit(current);
}

gf::Size VectorField::measure(gf::Size available) {
    const gf::Size size{available.width, std::min(available.height, 24.0)};
    return size;
}

void VectorField::arrange(gf::Rect final_bounds) {
    arrange_self(final_bounds);
    const double width = std::max(0.0, (final_bounds.width - 2.0 * gap) / 3.0);
    for (std::size_t axis = 0U; axis < bars_.size(); ++axis) {
        const double x = static_cast<double>(axis) * (width + gap);
        set_child_layout(bars_[axis], gf::Rect{x, 0.0, width, final_bounds.height});
    }
}

} // namespace imgui_class
