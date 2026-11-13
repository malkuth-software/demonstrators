#pragma once

// The ImPlot3D-class window's content (D5): a toolbar, one Plot3DView and a
// status line.
//
//   toolbar   scene chooser, interpolation (CONV, linear, flat), stride
//             (1, 2, 4, 8, 16), panes, grid, orthographic, labels along
//             axes, Reset
//   plot      the Plot3DView showing the chosen scene
//   status    the hovered point's readout (matplotlib's coordinate
//             format), or the camera's angles and zoom when nothing is
//             hovered
//
// Scenes:
//   peaks         MATLAB-style peaks surface, colour mapped by z
//   sinc          sin(r)/r ripples
//   saddle_hole   a saddle with a NaN disc cut out (holes)
//   helix_cloud   a helix line through a normal scatter cloud
//   mixed         the peaks surface, a helix above it and a cloud
//
// Ownership: Plot3DRoot is a control (shared, owned by its window); the
// toolbar controls, the plot and the status line are its children. The
// data arrays are members, reused on every rebuild; the plot copies what
// it keeps (Plot3DScene's rule), so they may change after a rebuild.
//
// One UI thread.

#include "surface_data.hpp"

#include <gui_forms/control.hpp>
#include <gui_forms/controls/button_base.hpp>
#include <gui_forms/controls/check_box.hpp>
#include <gui_forms/controls/combo_box.hpp>
#include <gui_forms/controls/label.hpp>
#include <gui_forms/controls/plot3d_view.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace implot3d_class {

enum class SceneKind : std::uint8_t { peaks, sinc, saddle_hole, helix_cloud, mixed };
inline constexpr std::size_t scene_count = 5U;

struct Plot3DRootOptions final {
    // Samples per side of the scenes' surfaces.
    std::size_t grid_side{96U};
    // Points in the scatter cloud.
    std::size_t cloud_points{2000U};
};

struct Plot3DRootCounters final {
    std::uint64_t scene_switches{};
    std::uint64_t rebuilds{};
    std::uint64_t items_refused{};
    std::uint64_t hovers{};
    std::uint64_t picks{};
    std::uint64_t camera_changes{};
    std::uint64_t status_updates{};
};

class Plot3DRoot final : public gui_forms::Control {
public:
    static constexpr bool initialize_tree_after_construction = true;
    static constexpr double toolbar_height = 32.0;
    static constexpr double status_height = 22.0;
    static constexpr std::size_t stride_choice_count = 5U;

    Plot3DRoot(gui_forms::StableId stable_id, Plot3DRootOptions options);
    explicit Plot3DRoot(gui_forms::StableId stable_id);
    ~Plot3DRoot() override;
    Plot3DRoot(const Plot3DRoot&) = delete;
    Plot3DRoot& operator=(const Plot3DRoot&) = delete;
    void initialize_control_tree();

    // -- parts --------------------------------------------------------------------------
    [[nodiscard]] gui_forms::Plot3DView& plot() const noexcept { return *plot_; }
    [[nodiscard]] const std::shared_ptr<gui_forms::Plot3DView>& plot_pointer() const& noexcept { return plot_; }
    const std::shared_ptr<gui_forms::Plot3DView>& plot_pointer() const&& = delete;
    [[nodiscard]] gui_forms::Label& status() const noexcept { return *status_; }
    [[nodiscard]] const std::string& status_text() const& noexcept { return status_text_; }
    const std::string& status_text() const&& = delete;
    [[nodiscard]] SceneKind scene() const noexcept { return scene_; }
    [[nodiscard]] Plot3DRootCounters counters() const noexcept { return counters_; }
    [[nodiscard]] std::size_t grid_side() const noexcept { return options_.grid_side; }
    [[nodiscard]] std::size_t stride() const noexcept { return stride_; }
    [[nodiscard]] gui_forms::plots::Plot3DInterpolation interpolation() const noexcept { return interpolation_; }
    // The surface items of the current scene (none for helix_cloud).
    [[nodiscard]] std::size_t surface_items() const noexcept { return surface_items_; }

    // -- operations (the toolbar, tests and workloads) -----------------------------------
    // Each rebuilds the scene's items when it changes something.
    void select_scene(SceneKind scene);
    void set_interpolation(gui_forms::plots::Plot3DInterpolation interpolation);
    // `stride` is used for rows and columns; at least 1.
    void set_stride(std::size_t stride);
    // Samples per side, clamped to [minimum_grid_side, maximum_grid_side].
    void set_grid_side(std::size_t side);
    void set_orthographic(bool orthographic);
    void reset_view();
    void refresh_status();

    [[nodiscard]] gui_forms::Size measure(gui_forms::Size available) override;
    void arrange(gui_forms::Rect final_bounds) override;

private:
    struct Tool final {
        std::shared_ptr<gui_forms::Control> control{};
        double width{};
    };

    void add_tool(std::shared_ptr<gui_forms::Control> control, double width);
    // Clears the plot and adds the scene's items from the members.
    void rebuild();
    void build_items();
    void add_surface_item(const SurfaceGrid& grid, const char* label);

    void on_scene_chosen(std::optional<std::size_t> index);
    void on_interpolation_chosen(std::optional<std::size_t> index);
    void on_stride_chosen(std::optional<std::size_t> index);
    void on_panes_toggled(bool checked);
    void on_grid_toggled(bool checked);
    void on_orthographic_toggled(bool checked);
    void on_along_axis_toggled(bool checked);
    void on_reset_clicked(gui_forms::ButtonBase& button);
    void on_hovered(const gui_forms::plots::Plot3DHit& hit);
    void on_picked(const gui_forms::plots::Plot3DHit& hit);
    void on_camera_changed(const gui_forms::plots::Plot3DCamera& camera);

    Plot3DRootOptions options_{};
    SceneKind scene_{SceneKind::peaks};
    gui_forms::plots::Plot3DInterpolation interpolation_{gui_forms::plots::Plot3DInterpolation::conv};
    std::size_t stride_{1U};
    std::size_t surface_items_{};
    // The scenes' data, refilled in place on a rebuild.
    SurfaceGrid surface_{};
    PointCloud helix_{};
    PointCloud cloud_{};
    std::vector<Tool> tools_{};
    std::shared_ptr<gui_forms::ComboBox> scene_box_{};
    std::shared_ptr<gui_forms::ComboBox> interpolation_box_{};
    std::shared_ptr<gui_forms::ComboBox> stride_box_{};
    std::shared_ptr<gui_forms::CheckBox> panes_box_{};
    std::shared_ptr<gui_forms::CheckBox> grid_box_{};
    std::shared_ptr<gui_forms::CheckBox> orthographic_box_{};
    std::shared_ptr<gui_forms::CheckBox> along_axis_box_{};
    std::shared_ptr<gui_forms::Plot3DView> plot_{};
    std::shared_ptr<gui_forms::Label> status_{};
    std::string status_text_{};
    Plot3DRootCounters counters_{};
};

[[nodiscard]] const char* scene_name(SceneKind scene) noexcept;

} // namespace implot3d_class
