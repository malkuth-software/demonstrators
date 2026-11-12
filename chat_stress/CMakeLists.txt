# chat-stress: the chat stress demonstrator (CHAT_STRESS_SPEC), built against
# an installed GUI.Forms SDK through its exported targets only.
#
#   cmake -S chat_stress -B build/chat_stress -G Ninja \
#         -DCMAKE_PREFIX_PATH=<GUI.Forms prefix> \
#         -DCMAKE_TOOLCHAIN_FILE=<the SDK's LLVM 22 toolchain file>
#   cmake --build build/chat_stress
#   ctest --test-dir build/chat_stress --output-on-failure
#
# Licence: PolyForm Noncommercial 1.0.0 (repository LICENSE). GUI.Forms is MIT.

cmake_minimum_required(VERSION 3.25)
project(chat_stress VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(GUIForms 0.6 CONFIG REQUIRED COMPONENTS Controls)

# Sources that need only the portable libraries (Core and Controls): the
# corpus, the row painter, the chat content, the workload driver and the
# records. Tests link them as a static library with GUIForms::Controls.
set(CHAT_STRESS_MODEL_SOURCES
    src/avatar_atlas.cpp
    src/chat_rows.cpp
    src/chat_view.cpp
    src/corpus.cpp
    src/measurement.cpp
    src/options.cpp
    src/process_stats.cpp
    src/row_layout.cpp
    src/workload_driver.cpp)

# Sources that need the application library (the host run, the offscreen
# raster): the window, the headless setup collection and main.
set(CHAT_STRESS_APPLICATION_SOURCES
    src/chat_window.cpp
    src/setup_collection.cpp
    src/main.cpp)

function(chat_stress_configure target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE
        CHAT_STRESS_GUI_FORMS_VERSION="${GUIForms_VERSION}"
        CHAT_STRESS_BUILD_TYPE="$<CONFIG>")
    # Keep the selected SDK ahead of any older GUI.Forms installation.
    set_target_properties(${target} PROPERTIES NO_SYSTEM_FROM_IMPORTED TRUE)
    if(WIN32)
        target_link_libraries(${target} PRIVATE psapi advapi32)
    endif()
endfunction()

# GUI.Forms refuses a program that links both Core-only libraries and the
# Application library (its reject_mixed_core checks), so the application
# compiles every source itself, and the tests use a library of the model
# sources over Controls.
add_library(chat_stress_model STATIC ${CHAT_STRESS_MODEL_SOURCES})
target_link_libraries(chat_stress_model PUBLIC GUIForms::Controls)
chat_stress_configure(chat_stress_model)

if(TARGET GUIForms::Application)
    add_executable(chat-stress ${CHAT_STRESS_MODEL_SOURCES} ${CHAT_STRESS_APPLICATION_SOURCES})
    target_link_libraries(chat-stress PRIVATE GUIForms::Application)
    chat_stress_configure(chat-stress)
    if(WIN32)
        add_custom_command(TARGET chat-stress POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:GUIForms::Application>" "$<TARGET_FILE:GUIForms::Threading>"
                "$<TARGET_FILE_DIR:chat-stress>"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${GUIForms_FONT_DIR}" "$<TARGET_FILE_DIR:chat-stress>/fonts")
    endif()
else()
    message(STATUS "chat-stress: GUIForms::Application is not installed; building the headless tests only")
endif()

enable_testing()

foreach(test_name IN ITEMS corpus_tests row_layout_tests chat_view_tests)
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE chat_stress_model)
    chat_stress_configure(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 120)
endforeach()

if(TARGET GUIForms::Application)
    add_executable(offscreen_paint_tests tests/offscreen_paint_tests.cpp ${CHAT_STRESS_MODEL_SOURCES})
    target_link_libraries(offscreen_paint_tests PRIVATE GUIForms::Application)
    chat_stress_configure(offscreen_paint_tests)
    add_test(NAME offscreen_paint_tests COMMAND offscreen_paint_tests)
    set_tests_properties(offscreen_paint_tests PROPERTIES
        TIMEOUT 120
        ENVIRONMENT "GUI_FORMS_FONT_DIR=${GUIForms_FONT_DIR}")
endif()
