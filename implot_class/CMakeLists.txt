# implot-class: the ImPlot-class plotting demonstrator (D4,
# IMGUI_IMPLOT_STUDY part B), built against an installed GUI.Forms SDK
# through its exported targets only.
#
#   cmake -S implot_class -B build/implot_class -G Ninja \
#         -DCMAKE_PREFIX_PATH=<GUI.Forms prefix> \
#         -DCMAKE_TOOLCHAIN_FILE=<the SDK's LLVM 22 toolchain file>
#   cmake --build build/implot_class
#   ctest --test-dir build/implot_class --output-on-failure
#
# Optional: -DIMPLOT_CLASS_GD_LIBRARY=<path to the gui_drawing_c_api shared
# library of a GUI.Forms build tree> builds the gd_* export path
# (--export-png). The SDK does not install that library (GAPS.md G1), so
# the option is off by default.
#
# Licence: PolyForm Noncommercial 1.0.0 (repository LICENSE). GUI.Forms is MIT.

cmake_minimum_required(VERSION 3.25)
project(implot_class VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(GUIForms 0.6 CONFIG REQUIRED COMPONENTS Controls)
find_package(Threads REQUIRED)

set(IMPLOT_CLASS_GD_LIBRARY "" CACHE FILEPATH
    "The gui_drawing_c_api shared library (gd_get_api_v0) for the gd_* export path; empty leaves it out")
# The raster module (gdr_initialize), when the gd library is built without
# one registered. It has no public header (GAPS.md G5); gd_export.cpp
# declares the entry point itself.
set(IMPLOT_CLASS_GD_RASTER_LIBRARY "" CACHE FILEPATH
    "The gd_* raster module library (gdr_initialize); empty leaves it out")

# Sources that need only the portable libraries (Core, Controls and the
# plot primitives): the data, the custom plots, the drag model, the linked
# axes, the live feed, the root and the records. Tests link them as a static
# library with GUIForms::Controls.
set(IMPLOT_CLASS_MODEL_SOURCES
    src/drag_model.cpp
    src/drag_plot.cpp
    src/gd_export.cpp
    src/grid_page.cpp
    src/heatmap_plot.cpp
    src/histogram_plot.cpp
    src/json_writer.cpp
    src/linked_axes.cpp
    src/live_feed.cpp
    src/measurement.cpp
    src/options.cpp
    src/plot_frame.cpp
    src/plot_root.cpp
    src/series_data.cpp)

# Sources that need the application library (the host run, the offscreen
# raster): the window, the workloads and main.
set(IMPLOT_CLASS_APPLICATION_SOURCES
    src/plot_window.cpp
    src/workloads.cpp
    src/main.cpp)

function(implot_class_configure target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE
        IMPLOT_CLASS_GUI_FORMS_VERSION="${GUIForms_VERSION}"
        IMPLOT_CLASS_BUILD_TYPE="$<CONFIG>")
    if(IMPLOT_CLASS_GD_LIBRARY)
        target_compile_definitions(${target} PRIVATE IMPLOT_CLASS_WITH_GD=1)
        if(IMPLOT_CLASS_GD_RASTER_LIBRARY)
            target_compile_definitions(${target} PRIVATE IMPLOT_CLASS_WITH_GD_RASTER=1)
        endif()
    endif()
    # Keep the selected SDK ahead of any older GUI.Forms installation.
    set_target_properties(${target} PROPERTIES NO_SYSTEM_FROM_IMPORTED TRUE)
endfunction()

function(implot_class_link_extras target scope)
    target_link_libraries(${target} ${scope} GUIForms::PlotPrimitives Threads::Threads)
    if(IMPLOT_CLASS_GD_LIBRARY)
        target_link_libraries(${target} ${scope} "${IMPLOT_CLASS_GD_LIBRARY}")
        if(IMPLOT_CLASS_GD_RASTER_LIBRARY)
            target_link_libraries(${target} ${scope} "${IMPLOT_CLASS_GD_RASTER_LIBRARY}")
        endif()
    endif()
endfunction()

# GUI.Forms refuses a program that links both Core-only libraries and the
# Application library (its reject_mixed_core checks), so the application
# compiles every source itself, and the tests use a library of the model
# sources over Controls.
add_library(implot_class_model STATIC ${IMPLOT_CLASS_MODEL_SOURCES})
target_link_libraries(implot_class_model PUBLIC GUIForms::Controls)
implot_class_link_extras(implot_class_model PUBLIC)
implot_class_configure(implot_class_model)

if(TARGET GUIForms::Application)
    add_executable(implot-class ${IMPLOT_CLASS_MODEL_SOURCES} ${IMPLOT_CLASS_APPLICATION_SOURCES})
    target_link_libraries(implot-class PRIVATE GUIForms::Application)
    implot_class_link_extras(implot-class PRIVATE)
    implot_class_configure(implot-class)
    # The hosts look for "fonts" beside the program, so the bundled faces
    # are copied there on every platform; Windows also needs the GUI.Forms
    # DLLs beside it (the other platforms use the rpath).
    add_custom_command(TARGET implot-class POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${GUIForms_FONT_DIR}" "$<TARGET_FILE_DIR:implot-class>/fonts")
    if(WIN32)
        add_custom_command(TARGET implot-class POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:GUIForms::Application>" "$<TARGET_FILE:GUIForms::Threading>"
                "$<TARGET_FILE_DIR:implot-class>")
    endif()
else()
    message(STATUS "implot-class: GUIForms::Application is not installed; building the headless tests only")
endif()

enable_testing()

foreach(test_name IN ITEMS series_data_tests drag_model_tests live_feed_tests)
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE implot_class_model)
    implot_class_configure(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 120)
endforeach()

if(TARGET GUIForms::Application)
    add_executable(offscreen_paint_tests tests/offscreen_paint_tests.cpp ${IMPLOT_CLASS_MODEL_SOURCES})
    target_link_libraries(offscreen_paint_tests PRIVATE GUIForms::Application)
    implot_class_link_extras(offscreen_paint_tests PRIVATE)
    implot_class_configure(offscreen_paint_tests)
    add_test(NAME offscreen_paint_tests COMMAND offscreen_paint_tests)
    set_tests_properties(offscreen_paint_tests PROPERTIES
        TIMEOUT 300
        ENVIRONMENT "GUI_FORMS_FONT_DIR=${GUIForms_FONT_DIR}")
endif()
