cmake_minimum_required(VERSION 3.20)

project(WiiMenuC VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(APPLE)
    set(WM_BACKEND "metal" CACHE STRING "Graphics backend: metal or gles2")
else()
    set(WM_BACKEND "gles2" CACHE STRING "Graphics backend: metal or gles2")
endif()
set_property(CACHE WM_BACKEND PROPERTY STRINGS metal gles2)

add_library(wii_menu_core STATIC
    src/arrow_interaction.c
    src/audio.c
    src/audio_sequence.c
    src/audio_wave.c
    src/board_scene.c
    src/board_address.c
    src/board_contact_store.c
    src/board_calendar.c
    src/board_compose.c
    src/board_keyboard.c
    src/keyboard_dictionary.c
    src/board_erase.c
    src/board_reader_scroll.c
    src/board_store.c
    src/scene_fader.c
    src/sd_scene.c
    src/settings_scene.c
    src/storage_scene.c
    src/catalog.c
    src/channel_animation.c
    src/channel_drag.c
    src/font_cache.c
    src/health_scene.c
    src/home_overlay.c
    src/image.c
    src/json.c
    src/layout_runtime.c
    src/layout_present.c
    src/material_prepare.c
    src/menu.c
    src/menu_restart.c
    src/menu_transition.c
    src/options_scene.c
    src/outline_font.c
    src/preview_scene.c
    src/pointer.c
    src/resource_ash.c
    src/resource_bmg.c
    src/resource_audio.c
    src/resource_rsar.c
    src/resource_font.c
    src/resource_layout.c
    src/resource_scene.c
    src/resource_tpl.c
    src/resource_u8.c
    src/saved_layout.c
    src/shared_font_export.c
    src/source_hit.c
    src/texture_cache.c
    src/ui.c
    src/viewport.c
)
target_include_directories(wii_menu_core PUBLIC include)
find_package(Threads REQUIRED)
target_link_libraries(wii_menu_core PUBLIC Threads::Threads)
if(UNIX AND NOT APPLE)
    target_link_libraries(wii_menu_core PUBLIC m)
endif()

if(APPLE)
    add_executable(wii-menu MACOSX_BUNDLE src/main.c)
    set_target_properties(wii-menu PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.tomoeko.wii-menu-c"
        MACOSX_BUNDLE_BUNDLE_NAME "Wii Menu in C"
    )
else()
    add_executable(wii-menu src/main.c)
endif()

add_executable(wm-wad-extract tools/wad/crypto.c tools/wad/extract.c)
add_executable(wm-prepare tools/prepare.c tools/wad/crypto.c src/json.c)
target_include_directories(wm-prepare PRIVATE include)
add_executable(wm-nand-extract tools/wad/crypto.c tools/nand/reader.c
                               tools/nand/extract.c)
target_link_libraries(wii-menu PRIVATE wii_menu_core)
add_executable(wm-layout-export tools/layout_export.c)
add_executable(wm-keyboard-dictionary-export
               tools/keyboard_dictionary/export.c)
add_executable(wm-settings-export tools/settings/export.c
                                  tools/settings/gif.c
                                  tools/settings/png.c)
add_executable(wm-outline-font-export tools/settings/outline_font_export.c
                                      src/outline_font.c)
target_compile_definitions(wm-outline-font-export PRIVATE
    WM_OUTLINE_FONT_PARSE_ONLY)
add_executable(wm-restart-export tools/restart/export.c)
target_link_libraries(wm-restart-export PRIVATE wii_menu_core)
target_link_libraries(wm-layout-export PRIVATE wii_menu_core)
target_link_libraries(wm-keyboard-dictionary-export PRIVATE wii_menu_core)
add_dependencies(wm-prepare wm-keyboard-dictionary-export)
target_link_libraries(wm-settings-export PRIVATE wii_menu_core)
target_link_libraries(wm-outline-font-export PRIVATE wii_menu_core m)
add_executable(wm-audio-export tools/audio/export.c)
target_link_libraries(wm-audio-export PRIVATE wii_menu_core m)
add_executable(wm-shared-font-export tools/shared_font_export.c)
target_link_libraries(wm-shared-font-export PRIVATE wii_menu_core m)
add_executable(wm-channel-export tools/channels/export.c tools/channels/md5.c
                                 tools/wad/crypto.c)
target_link_libraries(wm-channel-export PRIVATE wii_menu_core m)

if(WM_BACKEND STREQUAL "metal")
    if(NOT APPLE)
        message(FATAL_ERROR "Metal requires an Apple platform.")
    endif()
    enable_language(OBJC)
    target_sources(wii-menu PRIVATE src/platform_metal.m src/audio_platform_apple.c)
    set_source_files_properties(src/platform_metal.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(wii-menu PRIVATE
        "-framework AppKit"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework Foundation"
        "-framework AudioToolbox"
    )
elseif(WM_BACKEND STREQUAL "gles2")
    if(APPLE)
        message(FATAL_ERROR "The macOS SDK has no OpenGL ES 2.0 runtime; use Metal.")
    endif()
    find_package(X11 REQUIRED)
    find_library(EGL_LIBRARY EGL REQUIRED)
    find_library(GLES2_LIBRARY GLESv2 REQUIRED)
    target_sources(wii-menu PRIVATE src/platform_gles2.c src/audio_platform_linux.c)
    target_link_libraries(wii-menu PRIVATE X11::X11 ${EGL_LIBRARY} ${GLES2_LIBRARY} m dl)
else()
    message(FATAL_ERROR "WM_BACKEND must be metal or gles2.")
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(wii_menu_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    target_compile_options(wii-menu PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(wm-wad-extract PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(wm-prepare PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(wm-nand-extract PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(wm-channel-export PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(wm-audio-export PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Werror)
    target_compile_options(wm-keyboard-dictionary-export PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Werror)
    target_compile_options(wm-settings-export PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Werror)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(wii-menu-keyboard-dictionary-test
                   tests/keyboard_dictionary_test.c)
    target_link_libraries(wii-menu-keyboard-dictionary-test PRIVATE
                          wii_menu_core)
    add_test(NAME wii-menu-keyboard-dictionary
             COMMAND wii-menu-keyboard-dictionary-test)
    add_executable(wii-menu-arrow-interaction-test
                   tests/arrow_interaction_test.c)
    target_link_libraries(wii-menu-arrow-interaction-test PRIVATE
                          wii_menu_core m)
    add_test(NAME wii-menu-arrow-interaction
             COMMAND wii-menu-arrow-interaction-test)
    add_executable(wii-menu-prepare-update-test tests/prepare_update_test.c
                                                src/json.c tools/wad/crypto.c)
    target_include_directories(wii-menu-prepare-update-test PRIVATE include tools)
    add_test(NAME wii-menu-prepare-update COMMAND wii-menu-prepare-update-test)
    add_executable(wii-menu-settings-gif-test tests/settings_gif_test.c
                                               tools/settings/gif.c)
    target_link_libraries(wii-menu-settings-gif-test PRIVATE wii_menu_core)
    add_test(NAME wii-menu-settings-gif COMMAND wii-menu-settings-gif-test)
    add_executable(wii-menu-settings-png-test tests/settings_png_test.c
                                               tools/settings/png.c)
    target_link_libraries(wii-menu-settings-png-test PRIVATE wii_menu_core)
    add_test(NAME wii-menu-settings-png COMMAND wii-menu-settings-png-test)
    add_executable(wii-menu-core-test tests/core_test.c)
    target_link_libraries(wii-menu-core-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-core COMMAND wii-menu-core-test)
    set_tests_properties(wii-menu-core PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-transition-test tests/menu_transition_test.c)
    target_link_libraries(wii-menu-transition-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-transition COMMAND wii-menu-transition-test)
    add_executable(wii-menu-crypto-test tools/wad/crypto.c tools/wad/test_crypto.c)
    add_test(NAME wii-menu-crypto COMMAND wii-menu-crypto-test)
    add_executable(wii-menu-resource-test tests/resource_formats_test.c)
    target_link_libraries(wii-menu-resource-test PRIVATE wii_menu_core)
    add_test(NAME wii-menu-resource COMMAND wii-menu-resource-test)
    add_executable(wii-menu-bmg-test tests/resource_bmg_test.c)
    target_link_libraries(wii-menu-bmg-test PRIVATE wii_menu_core)
    add_test(NAME wii-menu-bmg COMMAND wii-menu-bmg-test)
    set_tests_properties(wii-menu-bmg PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-layout-test tests/layout_runtime_test.c)
    target_link_libraries(wii-menu-layout-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-layout COMMAND wii-menu-layout-test)
    set_tests_properties(wii-menu-layout PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-font-test tests/resource_font_test.c)
    target_link_libraries(wii-menu-font-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-font COMMAND wii-menu-font-test)
    add_executable(wii-menu-outline-font-test tests/outline_font_test.c)
    target_link_libraries(wii-menu-outline-font-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-outline-font COMMAND wii-menu-outline-font-test)
    add_executable(wii-menu-outline-font-export-test
                   tests/outline_font_export_test.c)
    add_test(NAME wii-menu-outline-font-export
             COMMAND wii-menu-outline-font-export-test
                     $<TARGET_FILE:wm-outline-font-export>)
    add_executable(wii-menu-font-presentation-test tests/font_presentation_test.c)
    target_link_libraries(wii-menu-font-presentation-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-font-presentation COMMAND wii-menu-font-presentation-test)
    set_tests_properties(wii-menu-font-presentation PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-resource-layout-test tests/resource_layout_test.c)
    target_link_libraries(wii-menu-resource-layout-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-resource-layout COMMAND wii-menu-resource-layout-test)
    add_executable(wii-menu-audio-test tests/resource_audio_test.c)
    target_link_libraries(wii-menu-audio-test PRIVATE wii_menu_core)
    add_test(NAME wii-menu-audio COMMAND wii-menu-audio-test)
    add_executable(wii-menu-audio-wave-test tests/audio_wave_test.c)
    target_link_libraries(wii-menu-audio-wave-test PRIVATE wii_menu_core)
    add_test(NAME wii-menu-audio-wave COMMAND wii-menu-audio-wave-test)
    add_executable(wii-menu-audio-restart-test tests/audio_restart_test.c)
    target_include_directories(wii-menu-audio-restart-test PRIVATE src)
    target_link_libraries(wii-menu-audio-restart-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-audio-restart COMMAND wii-menu-audio-restart-test)
    add_executable(wii-menu-audio-sequence-test tests/audio_sequence_test.c)
    target_link_libraries(wii-menu-audio-sequence-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-audio-sequence COMMAND wii-menu-audio-sequence-test)
    add_executable(wii-menu-home-overlay-test tests/home_overlay_test.c)
    target_link_libraries(wii-menu-home-overlay-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-home-overlay COMMAND wii-menu-home-overlay-test)
    set_tests_properties(wii-menu-home-overlay PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-source-hit-test tests/source_hit_test.c)
    target_link_libraries(wii-menu-source-hit-test PRIVATE wii_menu_core)
    add_test(NAME wii-menu-source-hit COMMAND wii-menu-source-hit-test)
    set_tests_properties(wii-menu-source-hit PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-saved-layout-test tests/saved_layout_test.c)
    target_link_libraries(wii-menu-saved-layout-test PRIVATE wii_menu_core)
    add_test(NAME wii-menu-saved-layout COMMAND wii-menu-saved-layout-test)
    add_executable(wii-menu-shared-font-export-test tests/shared_font_export_test.c)
    target_link_libraries(wii-menu-shared-font-export-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-shared-font-export COMMAND wii-menu-shared-font-export-test)
    add_executable(wii-menu-catalog-saved-test tests/catalog_saved_test.c)
    target_link_libraries(wii-menu-catalog-saved-test PRIVATE wii_menu_core)
    add_test(NAME wii-menu-catalog-saved COMMAND wii-menu-catalog-saved-test)
    set_tests_properties(wii-menu-catalog-saved PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-channel-animation-test tests/channel_animation_test.c)
    target_link_libraries(wii-menu-channel-animation-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-channel-animation COMMAND wii-menu-channel-animation-test)
    set_tests_properties(wii-menu-channel-animation PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-channel-drag-test tests/channel_drag_test.c)
    target_link_libraries(wii-menu-channel-drag-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-channel-drag COMMAND wii-menu-channel-drag-test)
    set_tests_properties(wii-menu-channel-drag PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-grid-tiles-test tests/grid_tiles_test.c)
    target_link_libraries(wii-menu-grid-tiles-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-grid-tiles COMMAND wii-menu-grid-tiles-test)
    set_tests_properties(wii-menu-grid-tiles PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-resource-scene-edge-test
        tests/resource_scene_edge_test.c)
    target_link_libraries(wii-menu-resource-scene-edge-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-resource-scene-edge
        COMMAND wii-menu-resource-scene-edge-test)
    set_tests_properties(wii-menu-resource-scene-edge PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-resource-scene-parked-memo-test
        tests/resource_scene_parked_memo_test.c)
    target_link_libraries(wii-menu-resource-scene-parked-memo-test
        PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-resource-scene-parked-memo
        COMMAND wii-menu-resource-scene-parked-memo-test)
    set_tests_properties(wii-menu-resource-scene-parked-memo PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-viewport-test tests/viewport_test.c)
    target_link_libraries(wii-menu-viewport-test PRIVATE wii_menu_core)
    add_test(NAME wii-menu-viewport COMMAND wii-menu-viewport-test)
    add_executable(wii-menu-board-scene-test tests/board_scene_test.c)
    target_link_libraries(wii-menu-board-scene-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-board-scene COMMAND wii-menu-board-scene-test)
    set_tests_properties(wii-menu-board-scene PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-board-address-test tests/board_address_test.c)
    target_link_libraries(wii-menu-board-address-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-board-address COMMAND wii-menu-board-address-test)
    set_tests_properties(wii-menu-board-address PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-board-contact-store-test
        tests/board_contact_store_test.c)
    target_link_libraries(wii-menu-board-contact-store-test
        PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-board-contact-store
        COMMAND wii-menu-board-contact-store-test)
    add_executable(wii-menu-board-caret-draw-test
        tests/board_caret_draw_test.c)
    target_link_libraries(wii-menu-board-caret-draw-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-board-caret-draw
        COMMAND wii-menu-board-caret-draw-test)
    set_tests_properties(wii-menu-board-caret-draw PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-board-keyboard-artwork-test
        tests/board_keyboard_artwork_test.c)
    target_link_libraries(wii-menu-board-keyboard-artwork-test
        PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-board-keyboard-artwork
        COMMAND wii-menu-board-keyboard-artwork-test)
    set_tests_properties(wii-menu-board-keyboard-artwork PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-board-compose-order-test
        tests/board_compose_order_test.c)
    target_link_libraries(wii-menu-board-compose-order-test
        PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-board-compose-order
        COMMAND wii-menu-board-compose-order-test)
    set_tests_properties(wii-menu-board-compose-order PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-board-calendar-arrow-test
        tests/board_calendar_arrow_test.c)
    target_link_libraries(wii-menu-board-calendar-arrow-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-board-calendar-arrow
        COMMAND wii-menu-board-calendar-arrow-test)
    set_tests_properties(wii-menu-board-calendar-arrow PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-board-reader-scroll-test tests/board_reader_scroll_test.c)
    target_link_libraries(wii-menu-board-reader-scroll-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-board-reader-scroll COMMAND wii-menu-board-reader-scroll-test)
    add_executable(wii-menu-health-scene-test tests/health_scene_test.c)
    target_link_libraries(wii-menu-health-scene-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-health-scene COMMAND wii-menu-health-scene-test)
    set_tests_properties(wii-menu-health-scene PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-options-scene-test tests/options_scene_test.c)
    target_link_libraries(wii-menu-options-scene-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-options-scene COMMAND wii-menu-options-scene-test)
    set_tests_properties(wii-menu-options-scene PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-settings-scene-test tests/settings_scene_test.c)
    target_link_libraries(wii-menu-settings-scene-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-settings-scene COMMAND wii-menu-settings-scene-test)
    set_tests_properties(wii-menu-settings-scene PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-pointer-grab-test tests/pointer_grab_test.c)
    target_link_libraries(wii-menu-pointer-grab-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-pointer-grab COMMAND wii-menu-pointer-grab-test)
    set_tests_properties(wii-menu-pointer-grab PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-restart-test tests/menu_restart_test.c)
    target_link_libraries(wii-menu-restart-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-restart COMMAND wii-menu-restart-test)
    set_tests_properties(wii-menu-restart PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-scene-fader-test tests/scene_fader_test.c)
    target_link_libraries(wii-menu-scene-fader-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-scene-fader COMMAND wii-menu-scene-fader-test)
    add_executable(wii-menu-storage-scene-test tests/storage_scene_test.c)
    target_link_libraries(wii-menu-storage-scene-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-storage-scene COMMAND wii-menu-storage-scene-test)
    set_tests_properties(wii-menu-storage-scene PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-sd-scene-test tests/sd_scene_test.c)
    target_link_libraries(wii-menu-sd-scene-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-sd-scene COMMAND wii-menu-sd-scene-test)
    set_tests_properties(wii-menu-sd-scene PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-resource-clock-test tests/resource_scene_clock_test.c)
    target_link_libraries(wii-menu-resource-clock-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-resource-clock COMMAND wii-menu-resource-clock-test)
    set_tests_properties(wii-menu-resource-clock PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-resource-badge-test tests/resource_scene_badge_test.c)
    target_link_libraries(wii-menu-resource-badge-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-resource-badge COMMAND wii-menu-resource-badge-test)
    add_executable(wii-menu-resource-hover-test tests/resource_scene_hover_test.c)
    target_link_libraries(wii-menu-resource-hover-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-resource-hover COMMAND wii-menu-resource-hover-test)
    set_tests_properties(wii-menu-resource-hover PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-resource-arrow-transition-test
        tests/resource_scene_arrow_transition_test.c)
    target_link_libraries(wii-menu-resource-arrow-transition-test
        PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-resource-arrow-transition
        COMMAND wii-menu-resource-arrow-transition-test)
    set_tests_properties(wii-menu-resource-arrow-transition PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-resource-footer-balloon-test
        tests/resource_scene_footer_balloon_test.c)
    target_link_libraries(wii-menu-resource-footer-balloon-test
        PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-resource-footer-balloon
        COMMAND wii-menu-resource-footer-balloon-test)
    set_tests_properties(wii-menu-resource-footer-balloon PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-resource-board-balloon-test
        tests/resource_scene_board_balloon_test.c)
    target_link_libraries(wii-menu-resource-board-balloon-test
        PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-resource-board-balloon
        COMMAND wii-menu-resource-board-balloon-test)
    set_tests_properties(wii-menu-resource-board-balloon PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(wii-menu-preview-arrow-test tests/preview_arrow_test.c)
    target_link_libraries(wii-menu-preview-arrow-test PRIVATE wii_menu_core m)
    add_test(NAME wii-menu-preview-arrow COMMAND wii-menu-preview-arrow-test)
    set_tests_properties(wii-menu-preview-arrow PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(wii-menu-resource-badge PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    # The C tests use assert() for their checks, including some setup calls.
    # Keep those checks active when the application is built in Release mode.
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        get_property(test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
        foreach(test_target IN LISTS test_targets)
            if(test_target MATCHES "-test$")
                target_compile_options(${test_target} PRIVATE -UNDEBUG)
            endif()
        endforeach()
    endif()
endif()
