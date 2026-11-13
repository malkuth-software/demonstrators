# implot3d-class: the ImPlot3D-class plotting demonstrator (D5), built
# against an installed GUI.Forms SDK through its exported targets only.
#
#   cmake -S implot3d_class -B build/implot3d_class -G Ninja \
#         -DCMAKE_PREFIX_PATH=<GUI.Forms prefix> \
#         -DCMAKE_TOOLCHAIN_FILE=<the SDK's LLVM 22 toolchain file>
#   cmake --build build/implot3d_class
#   ctest --test-dir build/implot3d_class --output-on-failure
#
# Needs a GUI.Forms with Plot3DView (claude/cp33-plots3d or later), whose
# package finds stx_raster3d and stx_conv itself.
#
# Licence: PolyForm Noncommercial 1.0.0 (repository LICENSE). GUI.Forms is MIT.

cmake_minimum_required(VERSION 3.25)
project(implot3d_class VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(GUIForms 0.6 CONFIG REQUIRED COMPONENTS Controls)

# Sources that need only the portable libraries (Core, Controls and the
# plot primitives): the data, the root and the records.
set(IMPLOT3D_CLASS_MODEL_SOURCES
    src/json_writer.cpp
    src/measurement.cpp
    src/options.cpp
    src/plot3d_root.cpp
    src/surface_data.cpp)

# Sources that need the application library (the host run, the offscreen
# raster): the window, the workloads and main.
set(IMPLOT3D_CLASS_APPLICATION_SOURCES
    src/plot3d_window.cpp
    src/workloads.cpp
    src/main.cpp)

function(implot3d_class_configure target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE
        IMPLOT3D_CLASS_GUI_FORMS_VERSION="${GUIForms_VERSION}"
        IMPLOT3D_CLASS_BUILD_TYPE="$<CONFIG>")
    # Keep the selected SDK ahead of any older GUI.Forms installation.
    set_target_properties(${target} PROPERTIES NO_SYSTEM_FROM_IMPORTED TRUE)
endfunction()

# GUI.Forms refuses a program that links both Core-only libraries and the
# Application library (its reject_mixed_core checks), so the application
# compiles every source itself, and the data tests use a library of the
# model sources over Controls.
add_library(implot3d_class_model STATIC ${IMPLOT3D_CLASS_MODEL_SOURCES})
target_link_libraries(implot3d_class_model PUBLIC GUIForms::Controls GUIForms::PlotPrimitives)
implot3d_class_configure(implot3d_class_model)

if(TARGET GUIForms::Application)
    add_executable(implot3d-class ${IMPLOT3D_CLASS_MODEL_SOURCES} ${IMPLOT3D_CLASS_APPLICATION_SOURCES})
    target_link_libraries(implot3d-class PRIVATE GUIForms::Application GUIForms::PlotPrimitives)
    implot3d_class_configure(implot3d-class)
    # The hosts look for "fonts" beside the program, so the bundled faces
    # are copied there on every platform; Windows also needs the GUI.Forms
    # DLLs beside it (the other platforms use the rpath).
    add_custom_command(TARGET implot3d-class POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${GUIForms_FONT_DIR}" "$<TARGET_FILE_DIR:implot3d-class>/fonts")
    if(WIN32)
        add_custom_command(TARGET implot3d-class POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:GUIForms::Application>" "$<TARGET_FILE:GUIForms::Threading>"
                "$<TARGET_FILE_DIR:implot3d-class>")
    endif()
else()
    message(STATUS "implot3d-class: GUIForms::Application is not installed; building the data tests only")
endif()

enable_testing()

add_executable(surface_data_tests tests/surface_data_tests.cpp)
target_link_libraries(surface_data_tests PRIVATE implot3d_class_model)
implot3d_class_configure(surface_data_tests)
add_test(NAME surface_data_tests COMMAND surface_data_tests)
set_tests_properties(surface_data_tests PROPERTIES TIMEOUT 120)

if(TARGET GUIForms::Application)
    add_executable(offscreen_paint_tests tests/offscreen_paint_tests.cpp ${IMPLOT3D_CLASS_MODEL_SOURCES})
    target_link_libraries(offscreen_paint_tests PRIVATE GUIForms::Application GUIForms::PlotPrimitives)
    implot3d_class_configure(offscreen_paint_tests)
    add_test(NAME offscreen_paint_tests COMMAND offscreen_paint_tests)
    set_tests_properties(offscreen_paint_tests PROPERTIES
        TIMEOUT 300
        ENVIRONMENT "GUI_FORMS_FONT_DIR=${GUIForms_FONT_DIR}")

    # Each workload as a test: exits 1 when an invariant failed.
    foreach(workload IN ITEMS tour orbit zoom pick strides interpolation resolution idle)
        add_test(NAME workload_${workload}
                 COMMAND implot3d-class --measure ${workload} --output-dir "${CMAKE_BINARY_DIR}"
                         --font-dir "${GUIForms_FONT_DIR}")
        set_tests_properties(workload_${workload} PROPERTIES TIMEOUT 300)
    endforeach()
endif()
