add_library(wii_menu_core STATIC
    animation/channel_animation.c
    animation/menu_transition.c
    animation/scene_fader.c

    audio/audio.c
    audio/audio_held.c
    audio/audio_sequence.c
    audio/audio_wave.c
    audio/hover_audio.c

    board/board_address.c
    board/board_calendar.c
    board/board_compose.c
    board/board_erase.c
    board/board_keyboard.c
    board/board_reader_scroll.c
    board/board_model.c
    board/board_scene.c
    board/keyboard_dictionary.c
    board/keyboard_text.c

    fonts/font_cache.c
    fonts/outline_font.c
    fonts/shared_font_export.c

    input/arrow_interaction.c
    input/channel_drag.c
    input/pointer.c
    input/source_hit.c

    layout/layout_present.c
    layout/layout_runtime.c

    menu/catalog.c
    menu/menu.c
    menu/menu_restart.c

    persistence/atomic_file.c
    persistence/board_contact_store.c
    persistence/board_store.c
    persistence/saved_layout.c

    render/image.c
    render/material_prepare.c
    render/texture_cache.c
    render/ui.c
    render/viewport.c

    resources/resource_ash.c
    resources/resource_audio.c
    resources/resource_bmg.c
    resources/resource_font.c
    resources/resource_layout.c
    resources/resource_rsar.c
    resources/resource_tpl.c
    resources/resource_u8.c

    scenes/health_scene.c
    scenes/home_overlay.c
    scenes/options_scene.c
    scenes/preview_scene.c
    scenes/resource_scene.c
    scenes/scene_assets.c
    scenes/sd_scene.c
    scenes/settings_scene.c
    scenes/storage_scene.c

    support/json.c
)
target_include_directories(wii_menu_core PUBLIC "${PROJECT_SOURCE_DIR}/include")
wm_enable_warnings(wii_menu_core CONVERSION)

find_package(Threads REQUIRED)
target_link_libraries(wii_menu_core PUBLIC Threads::Threads)
if(UNIX AND NOT APPLE)
    target_link_libraries(wii_menu_core PUBLIC m)
endif()

if(WM_BUILD_APP)
    set(WM_APP_SOURCES
        app/frame_render.c
        app/main.c
        app/input_routing.c
    )
    if(APPLE)
        add_executable(wii-menu MACOSX_BUNDLE ${WM_APP_SOURCES})
        set_target_properties(wii-menu PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER "org.tomoeko.wii-menu-c"
            MACOSX_BUNDLE_BUNDLE_NAME "Wii Menu in C"
        )
    else()
        add_executable(wii-menu ${WM_APP_SOURCES})
    endif()
    target_include_directories(wii-menu PRIVATE audio render)
    target_link_libraries(wii-menu PRIVATE wii_menu_core)
    wm_enable_warnings(wii-menu)
    include("${PROJECT_SOURCE_DIR}/cmake/WiiMenuBackend.cmake")
endif()
