# flame-graph: the flame graph and profiler viewer (D2, roadmap row 4.5),
# built against an installed GUI.Forms SDK through its exported targets
# only.
#
#   cmake -S flame_graph -B build/flame_graph -G Ninja \
#         -DCMAKE_PREFIX_PATH=<GUI.Forms prefix> \
#         -DCMAKE_TOOLCHAIN_FILE=<the SDK's LLVM 22 toolchain file>
#   cmake --build build/flame_graph
#   ctest --test-dir build/flame_graph --output-on-failure
#
# Licence: PolyForm Noncommercial 1.0.0 (repository LICENSE). GUI.Forms is MIT.

cmake_minimum_required(VERSION 3.25)
project(flame_graph VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(GUIForms 0.6 CONFIG REQUIRED COMPONENTS Controls)

# Sources with no GUI.Forms dependency beyond the colour type: the string
# table, the JSON reader, the model, the loaders and the generator. The
# model tests and loader tests link only these (with Controls for the
# headers' include paths).
set(FLAME_GRAPH_TRACE_SOURCES
    src/json_reader.cpp
    src/profile_ink.cpp
    src/string_table.cpp
    src/trace_generator.cpp
    src/trace_loader.cpp
    src/trace_model.cpp)

# Sources that need the portable libraries (Core, Controls and the plot
# primitives): the views, the table, the root, the records.
set(FLAME_GRAPH_VIEW_SOURCES
    src/details_table.cpp
    src/flame_view.cpp
    src/json_writer.cpp
    src/measurement.cpp
    src/options.cpp
    src/profiler_root.cpp
    src/timeline_view.cpp)

# Sources that need the application library (the host run, the offscreen
# raster): the window, the workloads and main.
set(FLAME_GRAPH_APPLICATION_SOURCES
    src/profiler_window.cpp
    src/workloads.cpp
    src/main.cpp)

function(flame_graph_configure target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE
        FLAME_GRAPH_GUI_FORMS_VERSION="${GUIForms_VERSION}"
        FLAME_GRAPH_BUILD_TYPE="$<CONFIG>")
    # Keep the selected SDK ahead of any older GUI.Forms installation.
    set_target_properties(${target} PROPERTIES NO_SYSTEM_FROM_IMPORTED TRUE)
endfunction()

# GUI.Forms refuses a program that links both Core-only libraries and the
# Application library (its reject_mixed_core checks), so the application
# compiles every source itself, and the tests use a library of the model
# and view sources over Controls.
add_library(flame_graph_model STATIC ${FLAME_GRAPH_TRACE_SOURCES} ${FLAME_GRAPH_VIEW_SOURCES})
target_link_libraries(flame_graph_model PUBLIC GUIForms::Controls GUIForms::PlotPrimitives)
flame_graph_configure(flame_graph_model)

if(TARGET GUIForms::Application)
    add_executable(flame-graph ${FLAME_GRAPH_TRACE_SOURCES} ${FLAME_GRAPH_VIEW_SOURCES} ${FLAME_GRAPH_APPLICATION_SOURCES})
    target_link_libraries(flame-graph PRIVATE GUIForms::Application GUIForms::PlotPrimitives)
    flame_graph_configure(flame-graph)
    # The hosts look for "fonts" beside the program, so the bundled faces
    # are copied there on every platform; Windows also needs the GUI.Forms
    # DLLs beside it (the other platforms use the rpath).
    add_custom_command(TARGET flame-graph POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${GUIForms_FONT_DIR}" "$<TARGET_FILE_DIR:flame-graph>/fonts")
    if(WIN32)
        add_custom_command(TARGET flame-graph POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:GUIForms::Application>" "$<TARGET_FILE:GUIForms::Threading>"
                "$<TARGET_FILE_DIR:flame-graph>")
    endif()
else()
    message(STATUS "flame-graph: GUIForms::Application is not installed; building the headless tests only")
endif()

enable_testing()

foreach(test_name IN ITEMS model_tests loader_tests view_tests)
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE flame_graph_model)
    flame_graph_configure(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 120)
endforeach()

if(TARGET GUIForms::Application)
    add_executable(offscreen_paint_tests tests/offscreen_paint_tests.cpp
        ${FLAME_GRAPH_TRACE_SOURCES} ${FLAME_GRAPH_VIEW_SOURCES})
    target_link_libraries(offscreen_paint_tests PRIVATE GUIForms::Application GUIForms::PlotPrimitives)
    flame_graph_configure(offscreen_paint_tests)
    add_test(NAME offscreen_paint_tests COMMAND offscreen_paint_tests)
    set_tests_properties(offscreen_paint_tests PROPERTIES
        TIMEOUT 300
        ENVIRONMENT "GUI_FORMS_FONT_DIR=${GUIForms_FONT_DIR}")
endif()
