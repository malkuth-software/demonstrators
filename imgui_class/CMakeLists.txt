# imgui-class: the ImGui-class widget demonstrator (D3, IMGUI_IMPLOT_STUDY
# part A), built against an installed GUI.Forms SDK through its exported
# targets only.
#
#   cmake -S imgui_class -B build/imgui_class -G Ninja \
#         -DCMAKE_PREFIX_PATH=<GUI.Forms prefix> \
#         -DCMAKE_TOOLCHAIN_FILE=<the SDK's LLVM 22 toolchain file>
#   cmake --build build/imgui_class
#   ctest --test-dir build/imgui_class --output-on-failure
#
# Licence: PolyForm Noncommercial 1.0.0 (repository LICENSE). GUI.Forms is MIT.

cmake_minimum_required(VERSION 3.25)
project(imgui_class VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(GUIForms 0.6 CONFIG REQUIRED COMPONENTS Controls)

# Sources that need only the portable libraries (Core, Controls and the
# plot primitives): the model, the colour arithmetic, the custom controls,
# the root and the records. Tests link them as a static library with
# GUIForms::Controls.
set(IMGUI_CLASS_MODEL_SOURCES
    src/color_field.cpp
    src/color_math.cpp
    src/demo_model.cpp
    src/demo_root.cpp
    src/form_stack.cpp
    src/grid_page.cpp
    src/json_writer.cpp
    src/measurement.cpp
    src/options.cpp
    src/vector_field.cpp)

# Sources that need the application library (the host run, the offscreen
# raster): the window, the workloads and main.
set(IMGUI_CLASS_APPLICATION_SOURCES
    src/demo_window.cpp
    src/workloads.cpp
    src/main.cpp)

function(imgui_class_configure target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE
        IMGUI_CLASS_GUI_FORMS_VERSION="${GUIForms_VERSION}"
        IMGUI_CLASS_BUILD_TYPE="$<CONFIG>")
    # Keep the selected SDK ahead of any older GUI.Forms installation.
    set_target_properties(${target} PROPERTIES NO_SYSTEM_FROM_IMPORTED TRUE)
endfunction()

# GUI.Forms refuses a program that links both Core-only libraries and the
# Application library (its reject_mixed_core checks), so the application
# compiles every source itself, and the tests use a library of the model
# sources over Controls.
add_library(imgui_class_model STATIC ${IMGUI_CLASS_MODEL_SOURCES})
target_link_libraries(imgui_class_model PUBLIC GUIForms::Controls GUIForms::PlotPrimitives)
imgui_class_configure(imgui_class_model)

if(TARGET GUIForms::Application)
    add_executable(imgui-class ${IMGUI_CLASS_MODEL_SOURCES} ${IMGUI_CLASS_APPLICATION_SOURCES})
    target_link_libraries(imgui-class PRIVATE GUIForms::Application GUIForms::PlotPrimitives)
    imgui_class_configure(imgui-class)
    # The hosts look for "fonts" beside the program, so the bundled faces
    # are copied there on every platform; Windows also needs the GUI.Forms
    # DLLs beside it (the other platforms use the rpath).
    add_custom_command(TARGET imgui-class POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${GUIForms_FONT_DIR}" "$<TARGET_FILE_DIR:imgui-class>/fonts")
    if(WIN32)
        add_custom_command(TARGET imgui-class POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:GUIForms::Application>" "$<TARGET_FILE:GUIForms::Threading>"
                "$<TARGET_FILE_DIR:imgui-class>")
    endif()
else()
    message(STATUS "imgui-class: GUIForms::Application is not installed; building the headless tests only")
endif()

enable_testing()

foreach(test_name IN ITEMS color_math_tests demo_model_tests)
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE imgui_class_model)
    imgui_class_configure(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 120)
endforeach()

if(TARGET GUIForms::Application)
    add_executable(offscreen_paint_tests tests/offscreen_paint_tests.cpp ${IMGUI_CLASS_MODEL_SOURCES})
    target_link_libraries(offscreen_paint_tests PRIVATE GUIForms::Application GUIForms::PlotPrimitives)
    imgui_class_configure(offscreen_paint_tests)
    add_test(NAME offscreen_paint_tests COMMAND offscreen_paint_tests)
    set_tests_properties(offscreen_paint_tests PROPERTIES
        TIMEOUT 300
        ENVIRONMENT "GUI_FORMS_FONT_DIR=${GUIForms_FONT_DIR}")
endif()
