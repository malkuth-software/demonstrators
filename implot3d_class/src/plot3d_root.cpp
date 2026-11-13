#include "plot3d_root.hpp"

#include <gui_forms/controls/button.hpp>
#include <gui_forms/event.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace implot3d_class {

namespace gf = gui_forms;
namespace gp = gui_forms::plots;

namespace {

constexpr std::array<const char*, scene_count> scene_names{{"Peaks", "Sinc", "Saddle with hole", "Helix and cloud", "Mixed"}};
constexpr std::array<const char*, 3> interpolation_names{{"CONV", "Linear", "Flat"}};
constexpr std::array<gp::Plot3DInterpolation, 3> interpolation_values{{
    gp::Plot3DInterpolation::conv,
    gp::Plot3DInterpolation::linear,
    gp::Plot3DInterpolation::flat,
}};
constexpr std::array<std::size_t, Plot3DRoot::stride_choice_count> stride_values{{1U, 2U, 4U, 8U, 16U}};
constexpr std::array<const char*, Plot3DRoot::stride_choice_count> stride_names{{"1", "2", "4", "8", "16"}};

constexpr std::size_t helix_points = 600U;
constexpr double helix_turns = 5.0;
constexpr std::uint64_t cloud_seed = 7U;

std::vector<std::string> names_of(std::span<const char* const> names) {
    std::vector<std::string> result{};
    result.reserve(names.size());
    for (const char* name : names) result.emplace_back(name);
    return result;
}

SurfaceKind surface_of(SceneKind scene) noexcept {
    switch (scene) {
    case SceneKind::sinc:
        return SurfaceKind::sinc;
    case SceneKind::saddle_hole:
        return SurfaceKind::saddle_with_hole;
    case SceneKind::peaks:
    case SceneKind::helix_cloud:
    case SceneKind::mixed:
    default:
        return SurfaceKind::peaks;
    }
}

} // namespace

const char* scene_name(SceneKind scene) noexcept {
    const std::size_t index = static_cast<std::size_t>(scene);
    if (index >= scene_names.size()) return "unknown";
    return scene_names[index];
}

Plot3DRoot::Plot3DRoot(gf::StableId stable_id, Plot3DRootOptions options)
    : gf::Control(std::move(stable_id)), options_(options) {
    options_.grid_side = std::clamp(options_.grid_side, minimum_grid_side, maximum_grid_side);
    status_text_.reserve(256U);
    tools_.reserve(8U);
}

Plot3DRoot::Plot3DRoot(gf::StableId stable_id) : Plot3DRoot(std::move(stable_id), Plot3DRootOptions{}) {}

Plot3DRoot::~Plot3DRoot() {}

void Plot3DRoot::add_tool(std::shared_ptr<gf::Control> control, double width) {
    add_child(control);
    tools_.push_back(Tool{std::move(control), width});
}

void Plot3DRoot::initialize_control_tree() {
    scene_box_ = gf::make_control<gf::ComboBox>(gf::StableId("implot3d.scene"));
    (*scene_box_).items = names_of(scene_names);
    (*scene_box_).selected_index = std::optional<std::size_t>{0U};
    interpolation_box_ = gf::make_control<gf::ComboBox>(gf::StableId("implot3d.interpolation"));
    (*interpolation_box_).items = names_of(interpolation_names);
    (*interpolation_box_).selected_index = std::optional<std::size_t>{0U};
    stride_box_ = gf::make_control<gf::ComboBox>(gf::StableId("implot3d.stride"));
    (*stride_box_).items = names_of(stride_names);
    (*stride_box_).selected_index = std::optional<std::size_t>{0U};
    panes_box_ = gf::make_control<gf::CheckBox>(gf::StableId("implot3d.panes"), "Panes");
    (*panes_box_).checked = true;
    grid_box_ = gf::make_control<gf::CheckBox>(gf::StableId("implot3d.grid"), "Grid");
    (*grid_box_).checked = true;
    orthographic_box_ = gf::make_control<gf::CheckBox>(gf::StableId("implot3d.orthographic"), "Orthographic");
    along_axis_box_ = gf::make_control<gf::CheckBox>(gf::StableId("implot3d.along-axis"), "Labels along axes");
    std::shared_ptr<gf::Button> reset = gf::make_control<gf::Button>(gf::StableId("implot3d.reset"), "Reset");

    add_tool(scene_box_, 160.0);
    add_tool(interpolation_box_, 100.0);
    add_tool(stride_box_, 64.0);
    add_tool(panes_box_, 80.0);
    add_tool(grid_box_, 70.0);
    add_tool(orthographic_box_, 120.0);
    add_tool(along_axis_box_, 150.0);
    add_tool(reset, 72.0);

    plot_ = gf::make_control<gf::Plot3DView>(gf::StableId("implot3d.plot"));
    (*plot_).accessible_name = std::string("3D plot");
    (*plot_).set_axis_title(gp::Plot3DAxis::x, "x");
    (*plot_).set_axis_title(gp::Plot3DAxis::y, "y");
    (*plot_).set_axis_title(gp::Plot3DAxis::z, "z");
    status_ = gf::make_control<gf::Label>(gf::StableId("implot3d.status"));
    add_child(plot_);
    add_child(status_);

    gf::on((*scene_box_).selected_index_changed(), *this, &Plot3DRoot::on_scene_chosen);
    gf::on((*interpolation_box_).selected_index_changed(), *this, &Plot3DRoot::on_interpolation_chosen);
    gf::on((*stride_box_).selected_index_changed(), *this, &Plot3DRoot::on_stride_chosen);
    gf::on((*panes_box_).checked_changed(), *this, &Plot3DRoot::on_panes_toggled);
    gf::on((*grid_box_).checked_changed(), *this, &Plot3DRoot::on_grid_toggled);
    gf::on((*orthographic_box_).checked_changed(), *this, &Plot3DRoot::on_orthographic_toggled);
    gf::on((*along_axis_box_).checked_changed(), *this, &Plot3DRoot::on_along_axis_toggled);
    gf::on((*reset).clicked(), *this, &Plot3DRoot::on_reset_clicked);
    gf::on((*plot_).hovered(), *this, &Plot3DRoot::on_hovered);
    gf::on((*plot_).picked(), *this, &Plot3DRoot::on_picked);
    gf::on((*plot_).camera_changed(), *this, &Plot3DRoot::on_camera_changed);

    rebuild();
    refresh_status();
}

// -- scenes ---------------------------------------------------------------------------------

void Plot3DRoot::add_surface_item(const SurfaceGrid& grid, const char* label) {
    gp::Plot3DSurfaceSpec spec{};
    spec.x = grid.xs;
    spec.y = grid.ys;
    spec.z = grid.zs;
    spec.rows = grid.rows;
    spec.columns = grid.columns;
    spec.row_stride = stride_;
    spec.column_stride = stride_;
    spec.interpolation = interpolation_;
    spec.label = label;
    static_cast<void>((*plot_).add_surface(spec));
    ++surface_items_;
}

void Plot3DRoot::rebuild() {
    (*plot_).clear_items();
    surface_items_ = 0U;
    // The plot refuses a shape it cannot draw (std::invalid_argument, the
    // plot unchanged); the demonstrator's data never should be, so a
    // refusal is counted and the items added before it stay.
    try {
        build_items();
    } catch (const std::invalid_argument& refusal) {
        static_cast<void>(refusal);
        ++counters_.items_refused;
    }
    ++counters_.rebuilds;
}

void Plot3DRoot::build_items() {
    const bool has_surface = scene_ != SceneKind::helix_cloud;
    const bool has_helix = scene_ == SceneKind::helix_cloud || scene_ == SceneKind::mixed;
    if (has_surface) {
        fill_surface(surface_, surface_of(scene_), options_.grid_side);
        add_surface_item(surface_, scene_name(scene_));
    }
    if (has_helix) {
        fill_helix(helix_, helix_points, helix_turns);
        // Above the peaks surface (which reaches about 8) in the mixed scene.
        if (scene_ == SceneKind::mixed) {
            for (std::size_t index = 0U; index < helix_.zs.size(); ++index) {
                helix_.xs[index] *= 2.0;
                helix_.ys[index] *= 2.0;
                helix_.zs[index] = 9.0 + 2.0 * helix_.zs[index];
            }
        }
        gp::Plot3DLineSpec line{};
        line.x = helix_.xs;
        line.y = helix_.ys;
        line.z = helix_.zs;
        line.width = 2.0;
        line.label = "helix";
        static_cast<void>((*plot_).add_line(line));

        fill_normal_cloud(cloud_, options_.cloud_points, scene_ == SceneKind::mixed ? 1.2 : 0.5, cloud_seed);
        gp::Plot3DScatterSpec scatter{};
        scatter.x = cloud_.xs;
        scatter.y = cloud_.ys;
        scatter.z = cloud_.zs;
        scatter.size = 4.0;
        scatter.label = "cloud";
        static_cast<void>((*plot_).add_scatter(scatter));
    }
}

// -- operations -----------------------------------------------------------------------------

void Plot3DRoot::select_scene(SceneKind scene) {
    if (scene == scene_) return;
    scene_ = scene;
    ++counters_.scene_switches;
    const std::optional<std::size_t> index{static_cast<std::size_t>(scene)};
    // Keeps the chooser in step; its event returns here and finds no change.
    if ((*scene_box_).selected_index.read() != index) (*scene_box_).selected_index = index;
    rebuild();
    refresh_status();
}

void Plot3DRoot::set_interpolation(gp::Plot3DInterpolation interpolation) {
    if (interpolation == interpolation_) return;
    interpolation_ = interpolation;
    for (std::size_t index = 0U; index < interpolation_values.size(); ++index) {
        if (interpolation_values[index] != interpolation) continue;
        const std::optional<std::size_t> selected{index};
        if ((*interpolation_box_).selected_index.read() != selected) (*interpolation_box_).selected_index = selected;
    }
    rebuild();
}

void Plot3DRoot::set_stride(std::size_t stride) {
    const std::size_t taken = std::max<std::size_t>(stride, 1U);
    if (taken == stride_) return;
    stride_ = taken;
    rebuild();
}

void Plot3DRoot::set_grid_side(std::size_t side) {
    const std::size_t clamped = std::clamp(side, minimum_grid_side, maximum_grid_side);
    if (clamped == options_.grid_side) return;
    options_.grid_side = clamped;
    rebuild();
    refresh_status();
}

void Plot3DRoot::set_orthographic(bool orthographic) {
    gp::Plot3DCamera camera = (*plot_).camera();
    camera.projection = orthographic ? gp::Plot3DProjection::orthographic : gp::Plot3DProjection::perspective;
    static_cast<void>((*plot_).set_camera(camera));
}

void Plot3DRoot::reset_view() {
    static_cast<void>((*plot_).reset_view());
}

void Plot3DRoot::refresh_status() {
    status_text_.clear();
    const std::string& readout = (*plot_).readout();
    if (!readout.empty()) {
        status_text_.append(readout);
    } else {
        const gp::Plot3DCamera& camera = (*plot_).camera();
        char buffer[160]{};
        const int written = std::snprintf(buffer, sizeof(buffer), "%s, %zu x %zu samples; azimuth %.0f, elevation %.0f, zoom %.2f",
                                          scene_name(scene_), options_.grid_side, options_.grid_side, camera.azimuth,
                                          camera.elevation, camera.zoom);
        if (written > 0) status_text_.append(buffer, std::min(static_cast<std::size_t>(written), sizeof(buffer) - 1U));
    }
    (*status_).text = status_text_;
    ++counters_.status_updates;
}

// -- events ---------------------------------------------------------------------------------

void Plot3DRoot::on_scene_chosen(std::optional<std::size_t> index) {
    if (!index.has_value() || *index >= scene_count) return;
    select_scene(static_cast<SceneKind>(*index));
}

void Plot3DRoot::on_interpolation_chosen(std::optional<std::size_t> index) {
    if (!index.has_value() || *index >= interpolation_values.size()) return;
    set_interpolation(interpolation_values[*index]);
}

void Plot3DRoot::on_stride_chosen(std::optional<std::size_t> index) {
    if (!index.has_value() || *index >= stride_values.size()) return;
    set_stride(stride_values[*index]);
}

void Plot3DRoot::on_panes_toggled(bool checked) {
    (*plot_).show_panes = checked;
}

void Plot3DRoot::on_grid_toggled(bool checked) {
    (*plot_).show_grid = checked;
}

void Plot3DRoot::on_orthographic_toggled(bool checked) {
    set_orthographic(checked);
}

void Plot3DRoot::on_along_axis_toggled(bool checked) {
    (*plot_).labels_along_axis = checked;
}

void Plot3DRoot::on_reset_clicked(gf::ButtonBase& button) {
    static_cast<void>(button);
    reset_view();
}

void Plot3DRoot::on_hovered(const gp::Plot3DHit& hit) {
    static_cast<void>(hit);
    ++counters_.hovers;
    refresh_status();
}

void Plot3DRoot::on_picked(const gp::Plot3DHit& hit) {
    static_cast<void>(hit);
    ++counters_.picks;
}

void Plot3DRoot::on_camera_changed(const gp::Plot3DCamera& camera) {
    ++counters_.camera_changes;
    // P on the keyboard switches the projection too: keep the box in step
    // (its event comes back to set_orthographic, which finds no change).
    const bool orthographic = camera.projection == gp::Plot3DProjection::orthographic;
    if ((*orthographic_box_).checked.read() != orthographic) (*orthographic_box_).checked = orthographic;
    refresh_status();
}

// -- layout ---------------------------------------------------------------------------------

gf::Size Plot3DRoot::measure(gf::Size available) {
    return available;
}

void Plot3DRoot::arrange(gf::Rect final_bounds) {
    arrange_self(final_bounds);
    constexpr double gap = 6.0;
    double x = gap;
    for (const Tool& tool : tools_) {
        set_child_layout(tool.control, gf::Rect{x, 4.0, tool.width, toolbar_height - 8.0});
        x += tool.width + gap;
    }
    const double plot_height = std::max(0.0, final_bounds.height - toolbar_height - status_height);
    set_child_layout(plot_, gf::Rect{0.0, toolbar_height, final_bounds.width, plot_height});
    set_child_layout(status_, gf::Rect{gap, toolbar_height + plot_height, std::max(0.0, final_bounds.width - 2.0 * gap),
                                       status_height});
}

} // namespace implot3d_class
