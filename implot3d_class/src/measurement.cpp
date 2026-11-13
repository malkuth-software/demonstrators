#include "measurement.hpp"

#include <gui_forms/allocation.hpp>
#include <gui_forms/window/window.hpp>

#include <fstream>
#include <system_error>

namespace implot3d_class {

namespace gf = gui_forms;
namespace gp = gui_forms::plots;

namespace {

void field(JsonWriter& json, std::string_view name, std::uint64_t value) {
    json.key(name);
    json.value(value);
}

void field(JsonWriter& json, std::string_view name, double value) {
    json.key(name);
    json.value(value);
}

} // namespace

SceneSnapshot read_scene(const gf::Plot3DView& plot) {
    const gp::Plot3DScene& scene = plot.scene();
    SceneSnapshot snapshot{};
    snapshot.picture_renders = scene.render_count();
    snapshot.mesh_builds = scene.mesh_build_count();
    snapshot.triangles_drawn = static_cast<std::uint64_t>(scene.triangles_drawn());
    snapshot.items = static_cast<std::uint64_t>(scene.item_count());
    snapshot.labels = static_cast<std::uint64_t>(scene.labels().size());
    snapshot.camera = scene.camera();
    return snapshot;
}

CounterSnapshot read_counters(gf::Window& window, const Plot3DRoot& root) {
    CounterSnapshot snapshot{};
    snapshot.root = root.counters();
    snapshot.scene = read_scene(root.plot());
    snapshot.metrics = window.metrics_snapshot();
    snapshot.allocations = gf::allocation_statistics();
    return snapshot;
}

void write_scene(JsonWriter& json, const SceneSnapshot& scene) {
    json.begin_object();
    field(json, "picture_renders", scene.picture_renders);
    field(json, "mesh_builds", scene.mesh_builds);
    field(json, "triangles_drawn", scene.triangles_drawn);
    field(json, "items", scene.items);
    field(json, "labels", scene.labels);
    json.key("camera");
    json.begin_object();
    field(json, "elevation", scene.camera.elevation);
    field(json, "azimuth", scene.camera.azimuth);
    field(json, "roll", scene.camera.roll);
    field(json, "zoom", scene.camera.zoom);
    json.key("projection");
    json.value(scene.camera.projection == gp::Plot3DProjection::orthographic ? "orthographic" : "perspective");
    json.end_object();
    json.end_object();
}

void write_counters(JsonWriter& json, const CounterSnapshot& snapshot) {
    json.begin_object();
    json.key("root");
    json.begin_object();
    field(json, "scene_switches", snapshot.root.scene_switches);
    field(json, "rebuilds", snapshot.root.rebuilds);
    field(json, "items_refused", snapshot.root.items_refused);
    field(json, "hovers", snapshot.root.hovers);
    field(json, "picks", snapshot.root.picks);
    field(json, "camera_changes", snapshot.root.camera_changes);
    field(json, "status_updates", snapshot.root.status_updates);
    json.end_object();
    json.key("scene");
    write_scene(json, snapshot.scene);
    // GUI.Forms' own counted allocation paths, process-wide since start;
    // its "scope" member says so. Not the process's allocations.
    json.key("gui_forms_allocations");
    json.raw(snapshot.allocations.to_json());
    json.key("window_metrics");
    json.raw(snapshot.metrics.to_json());
    json.end_object();
}

void write_build(JsonWriter& json) {
    json.begin_object();
    json.key("gui_forms_version");
    json.value(IMPLOT3D_CLASS_GUI_FORMS_VERSION);
    json.key("build_type");
    json.value(IMPLOT3D_CLASS_BUILD_TYPE);
    json.key("compiler");
#if defined(__clang__)
    json.value(__clang_version__);
#elif defined(_MSC_VER)
    json.value("msvc");
#else
    json.value("other");
#endif
    json.key("assertions");
#if defined(NDEBUG)
    json.value(false);
#else
    json.value(true);
#endif
    json.end_object();
}

std::string_view host_kind() noexcept {
#if defined(__APPLE__)
    return "macos";
#elif defined(_WIN32)
    return "windows";
#elif defined(__linux__)
    return "linux";
#else
    return "other";
#endif
}

std::filesystem::path record_path(const std::filesystem::path& root, std::string_view host, std::string_view workload) {
    std::string name(workload);
    name.append(".json");
    const std::filesystem::path path = root / "dev" / "measurements" / "implot3d_class" / std::string(host) / name;
    return path;
}

bool write_record_file(const std::filesystem::path& path, const std::string& text) {
    std::error_code error{};
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.put('\n');
    const bool written = static_cast<bool>(file);
    return written;
}

} // namespace implot3d_class
