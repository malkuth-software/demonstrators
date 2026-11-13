// implot-class: the ImPlot-class plotting demonstrator on GUI.Forms' public
// API (D4). With --measure it runs one workload headless and writes its
// record (docs/WORKLOADS.md); with --export-png it draws through the gd_*
// ABI and exits; otherwise it opens the window.

#include "gd_export.hpp"
#include "options.hpp"
#include "plot_window.hpp"
#include "series_data.hpp"
#include "workloads.hpp"

#include <gui_forms/application.hpp>

#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <vector>

namespace gf = gui_forms;

namespace {

int export_png(const implot_class::AppOptions& options) {
    const implot_class::XYSeries line = implot_class::make_random_walk(4000U, 3U);
    const implot_class::XYSeries scatter = implot_class::make_normal_cloud(2000U, 2000.0, 0.0, 600.0, 20.0, 5U);
    std::vector<double> bars{3.0, 4.5, 6.1, 7.2, 8.8, 9.4, 9.9, 9.1, 7.7, 6.0, 4.2, 3.1};
    std::vector<double> heatmap(64U * 64U);
    implot_class::fill_heatmap(heatmap, 64U, 64U, 5U);
    implot_class::GdExportInput input{};
    input.line = &line;
    input.scatter = &scatter;
    input.bars = bars;
    input.heatmap = heatmap;
    input.heatmap_rows = 64U;
    input.heatmap_columns = 64U;
    const implot_class::GdExportReport report = implot_class::export_plot_png(input, options.export_png);
    switch (report.status) {
    case implot_class::GdExportStatus::written:
        std::printf("implot-class: %llu commands, %llu PNG bytes written to %s\n",
                    static_cast<unsigned long long>(report.commands_executed),
                    static_cast<unsigned long long>(report.png_bytes), options.export_png.string().c_str());
        return 0;
    case implot_class::GdExportStatus::not_built:
        std::fprintf(stderr, "implot-class: built without the gd_* library (IMPLOT_CLASS_GD_LIBRARY)\n");
        return 2;
    case implot_class::GdExportStatus::api_unavailable:
        std::fprintf(stderr, "implot-class: gd_get_api_v0 refused ABI 0.3\n");
        return 1;
    case implot_class::GdExportStatus::call_failed:
        std::fprintf(stderr, "implot-class: %s failed (gd_result %u)\n", report.failed_call.c_str(), report.result);
        return 1;
    case implot_class::GdExportStatus::file_failed:
    default:
        std::fprintf(stderr, "implot-class: could not write %s\n", options.export_png.string().c_str());
        return 1;
    }
}

int run_window(gf::Application& application, const implot_class::AppOptions& options) {
    std::unique_ptr<implot_class::PlotWindow> window = std::make_unique<implot_class::PlotWindow>(application, options);
    gf::ApplicationWindowOptions window_options{};
    window_options.title = "ImPlot-class plots";
    window_options.initial_size = gf::Size{options.width, options.height};
    window_options.minimum_size = gf::Size{640.0, 420.0};
    const gf::ApplicationResult result = application.run(std::move(window), std::move(window_options));
    const int code = result.accepted() ? 0 : 1;
    return code;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const implot_class::OptionsResult parsed = implot_class::parse_options(argc, argv);
        if (!parsed.error.empty()) {
            std::fprintf(stderr, "implot-class: %s\n%s", parsed.error.c_str(), implot_class::usage_text());
            return 2;
        }
        const implot_class::AppOptions& options = parsed.options;
        if (options.help) {
            std::fputs(implot_class::usage_text(), stdout);
            return 0;
        }
        if (!options.export_png.empty()) return export_png(options);
        std::optional<implot_class::Workload> workload{};
        if (options.measure.has_value()) {
            workload = implot_class::workload_named(*options.measure);
            if (!workload.has_value()) {
                std::fprintf(stderr, "implot-class: unknown workload %s\n%s", (*options.measure).c_str(),
                             implot_class::usage_text());
                return 2;
            }
        }
        gf::ApplicationConfig config{};
        config.theme.base = gf::ThemeBase::gui_forms;
        gf::ApplicationCreateResult created = gf::Application::create(std::move(config));
        if (!created) {
            std::fprintf(stderr, "implot-class: the application could not be made\n");
            return 1;
        }
        int code = 0;
        {
            gf::Application& application = *created.application;
            if (workload.has_value()) {
                code = implot_class::run_workload(application, options, *workload);
            } else {
                code = run_window(application, options);
            }
        }
        // Once, after the last run (as GUI.Forms' reference application does).
        const gf::ApplicationResult ended = gf::Application::shutdown();
        if (!ended.accepted() && code == 0) code = 1;
        return code;
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "implot-class: %s\n", failure.what());
        return 1;
    }
}
