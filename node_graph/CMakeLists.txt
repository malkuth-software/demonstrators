# node-graph: the node graph demonstrator (NODE_GRAPH_SPEC), built against
# an installed GUI.Forms SDK through its exported targets only.
#
#   cmake -S node_graph -B build/node_graph -G Ninja \
#         -DCMAKE_PREFIX_PATH=<GUI.Forms prefix> \
#         -DCMAKE_TOOLCHAIN_FILE=<the SDK's LLVM 22 toolchain file>
#   cmake --build build/node_graph
#   ctest --test-dir build/node_graph --output-on-failure
#
# Licence: PolyForm Noncommercial 1.0.0 (repository LICENSE). GUI.Forms is MIT.

cmake_minimum_required(VERSION 3.25)
project(node_graph VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(GUIForms 0.6 CONFIG REQUIRED COMPONENTS Controls)

# Sources that need only the portable libraries (Core and Controls): the
# kinds and socket types, the document, the evaluator, the generator, the
# canvas (GUI.Forms' NodeCanvas, subclassed), the window content and the
# records. Tests link them
# as a static library with GUIForms::Controls.
set(NODE_GRAPH_MODEL_SOURCES
    src/evaluator.cpp
    src/generator.cpp
    src/graph_canvas.cpp
    src/graph_canvas_body.cpp
    src/graph_document.cpp
    src/graph_root.cpp
    src/json_writer.cpp
    src/kinds.cpp
    src/measurement.cpp
    src/options.cpp
    src/socket_types.cpp)

# Sources that need the application library (the host run, the offscreen
# raster): the window, the workloads and main.
set(NODE_GRAPH_APPLICATION_SOURCES
    src/graph_window.cpp
    src/workloads.cpp
    src/main.cpp)

function(node_graph_configure target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE
        NODE_GRAPH_GUI_FORMS_VERSION="${GUIForms_VERSION}"
        NODE_GRAPH_BUILD_TYPE="$<CONFIG>")
    # Keep the selected SDK ahead of any older GUI.Forms installation.
    set_target_properties(${target} PROPERTIES NO_SYSTEM_FROM_IMPORTED TRUE)
endfunction()

# GUI.Forms refuses a program that links both Core-only libraries and the
# Application library (its reject_mixed_core checks), so the application
# compiles every source itself, and the tests use a library of the model
# sources over Controls.
add_library(node_graph_model STATIC ${NODE_GRAPH_MODEL_SOURCES})
target_link_libraries(node_graph_model PUBLIC GUIForms::Controls)
node_graph_configure(node_graph_model)

if(TARGET GUIForms::Application)
    add_executable(node-graph ${NODE_GRAPH_MODEL_SOURCES} ${NODE_GRAPH_APPLICATION_SOURCES})
    target_link_libraries(node-graph PRIVATE GUIForms::Application)
    node_graph_configure(node-graph)
    # The hosts look for "fonts" beside the program, so the bundled faces
    # are copied there on every platform; Windows also needs the GUI.Forms
    # DLLs beside it (the other platforms use the rpath).
    add_custom_command(TARGET node-graph POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${GUIForms_FONT_DIR}" "$<TARGET_FILE_DIR:node-graph>/fonts")
    if(WIN32)
        add_custom_command(TARGET node-graph POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:GUIForms::Application>" "$<TARGET_FILE:GUIForms::Threading>"
                "$<TARGET_FILE_DIR:node-graph>")
    endif()
else()
    message(STATUS "node-graph: GUIForms::Application is not installed; building the headless tests only")
endif()

enable_testing()

foreach(test_name IN ITEMS model_tests document_tests canvas_tests)
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE node_graph_model)
    node_graph_configure(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 120)
endforeach()

if(TARGET GUIForms::Application)
    add_executable(offscreen_paint_tests tests/offscreen_paint_tests.cpp ${NODE_GRAPH_MODEL_SOURCES})
    target_link_libraries(offscreen_paint_tests PRIVATE GUIForms::Application)
    node_graph_configure(offscreen_paint_tests)
    add_test(NAME offscreen_paint_tests COMMAND offscreen_paint_tests)
    set_tests_properties(offscreen_paint_tests PROPERTIES
        TIMEOUT 120
        ENVIRONMENT "GUI_FORMS_FONT_DIR=${GUIForms_FONT_DIR}")
endif()
