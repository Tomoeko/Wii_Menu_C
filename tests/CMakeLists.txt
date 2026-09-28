# Animation.
wm_add_test(wii-menu-transition
    SOURCES animation/menu_transition_test.c
    LIBRARIES wii_menu_core m
)
wm_add_test(wii-menu-channel-animation
    SOURCES animation/channel_animation_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-scene-fader
    SOURCES animation/scene_fader_test.c
    LIBRARIES wii_menu_core m
)

# Audio.
wm_add_test(wii-menu-audio-held
    SOURCES audio/audio_held_test.c
    LIBRARIES wii_menu_core m
)
wm_add_test(wii-menu-audio-held-runtime
    SOURCES audio/audio_held_runtime_test.c
    LIBRARIES wii_menu_core m
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/audio"
)
wm_add_test(wii-menu-hover-audio
    SOURCES audio/hover_audio_test.c
    LIBRARIES wii_menu_core
)
wm_add_test(wii-menu-audio-wave
    SOURCES audio/audio_wave_test.c
    LIBRARIES wii_menu_core
)
wm_add_test(wii-menu-audio-restart
    SOURCES audio/audio_restart_test.c
    LIBRARIES wii_menu_core m
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/audio"
)
wm_add_test(wii-menu-audio-sequence
    SOURCES audio/audio_sequence_test.c
    LIBRARIES wii_menu_core m
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/audio"
)

# Board.
wm_add_test(wii-menu-keyboard-dictionary
    SOURCES board/keyboard_dictionary_test.c
    LIBRARIES wii_menu_core
)
wm_add_test(wii-menu-keyboard-text
    SOURCES board/keyboard_text_test.c
    LIBRARIES wii_menu_core
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/board"
)
wm_add_test(wii-menu-board-text
    SOURCES board/board_text_test.c
    LIBRARIES wii_menu_core
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/board"
)
wm_add_test(wii-menu-board-scene
    SOURCES board/board_scene_test.c
    LIBRARIES wii_menu_core m
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/board"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-board-address
    SOURCES board/board_address_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-board-caret-draw
    SOURCES board/board_caret_draw_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-board-keyboard-artwork
    SOURCES board/board_keyboard_artwork_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-board-compose-order
    SOURCES board/board_compose_order_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-board-calendar-arrow
    SOURCES board/board_calendar_arrow_test.c
    LIBRARIES wii_menu_core m
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/board"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-board-reader-scroll
    SOURCES board/board_reader_scroll_test.c
    LIBRARIES wii_menu_core m
)

# Fonts.
wm_add_test(wii-menu-outline-font
    SOURCES fonts/outline_font_test.c
    LIBRARIES wii_menu_core m
)
wm_add_test(wii-menu-font-presentation
    SOURCES fonts/font_presentation_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-shared-font-export
    SOURCES fonts/shared_font_export_test.c
    LIBRARIES wii_menu_core m
)

# Input.
wm_add_test(wii-menu-arrow-interaction
    SOURCES input/arrow_interaction_test.c
    LIBRARIES wii_menu_core m
)
wm_add_test(wii-menu-source-hit
    SOURCES input/source_hit_test.c
    LIBRARIES wii_menu_core
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-channel-drag
    SOURCES input/channel_drag_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-pointer-grab
    SOURCES input/pointer_grab_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

# Layout.
wm_add_test(wii-menu-layout
    SOURCES layout/layout_runtime_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-grid-tiles
    SOURCES layout/grid_tiles_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

# Menu.
wm_add_test(wii-menu-core
    SOURCES menu/core_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-catalog-saved
    SOURCES menu/catalog_saved_test.c
    LIBRARIES wii_menu_core
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-restart
    SOURCES menu/menu_restart_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

# Persistence.
wm_add_test(wii-menu-saved-layout
    SOURCES persistence/saved_layout_test.c
    LIBRARIES wii_menu_core
)
wm_add_test(wii-menu-board-contact-store
    SOURCES persistence/board_contact_store_test.c
    LIBRARIES wii_menu_core m
)
wm_add_test(wii-menu-atomic-file
    SOURCES persistence/atomic_file_test.c
    LIBRARIES wii_menu_core
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/support"
)

# Render.
wm_add_test(wii-menu-material-blend
    SOURCES render/material_blend_test.c
    LIBRARIES wii_menu_core
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/render"
)
wm_add_test(wii-menu-image-atomic
    SOURCES render/image_atomic_test.c
    LIBRARIES wii_menu_core
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/render"
)
wm_add_test(wii-menu-texture-source
    SOURCES render/texture_source_test.c
    LIBRARIES wii_menu_core
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/render"
)
wm_add_test(wii-menu-viewport
    SOURCES render/viewport_test.c
    LIBRARIES wii_menu_core
)
wm_add_test(wii-menu-render-geometry
    SOURCES render/render_geometry_test.c
    LIBRARIES wii_menu_core m
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/render"
)

# Resources.
wm_add_test(wii-menu-resource
    SOURCES resources/resource_formats_test.c
    LIBRARIES wii_menu_core
)
wm_add_test(wii-menu-rsar
    SOURCES resources/resource_rsar_test.c
    LIBRARIES wii_menu_core
)
wm_add_test(wii-menu-bmg
    SOURCES resources/resource_bmg_test.c
    LIBRARIES wii_menu_core
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-font
    SOURCES resources/resource_font_test.c
    LIBRARIES wii_menu_core m
)
wm_add_test(wii-menu-resource-layout
    SOURCES resources/resource_layout_test.c
    LIBRARIES wii_menu_core m
)
wm_add_test(wii-menu-audio
    SOURCES resources/resource_audio_test.c
    LIBRARIES wii_menu_core
)

# Scenes.
wm_add_test(wii-menu-scene-assets
    SOURCES scenes/scene_assets_test.c
    LIBRARIES wii_menu_core
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/scenes"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-home-overlay
    SOURCES scenes/home_overlay_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-resource-scene-edge
    SOURCES scenes/resource_scene_edge_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-resource-scene-parked-memo
    SOURCES scenes/resource_scene_parked_memo_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-health-scene
    SOURCES scenes/health_scene_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-options-scene
    SOURCES scenes/options_scene_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-settings-scene
    SOURCES scenes/settings_scene_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-storage-scene
    SOURCES scenes/storage_scene_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-sd-scene
    SOURCES scenes/sd_scene_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-resource-clock
    SOURCES scenes/resource_scene_clock_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-resource-badge
    SOURCES scenes/resource_scene_badge_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-resource-hover
    SOURCES scenes/resource_scene_hover_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-resource-arrow-transition
    SOURCES scenes/resource_scene_arrow_transition_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-resource-footer-balloon
    SOURCES scenes/resource_scene_footer_balloon_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-resource-board-balloon
    SOURCES scenes/resource_scene_board_balloon_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)
wm_add_test(wii-menu-preview-arrow
    SOURCES scenes/preview_arrow_test.c
    LIBRARIES wii_menu_core m
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

# Tools.
wm_add_test(wii-menu-export-directory
    SOURCES tools/export_directory_test.c
    LIBRARIES wm-export-directory wii_menu_core
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/support"
)
wm_add_test(wii-menu-channel-manifest
    SOURCES
        tools/channel_manifest_test.c
        "${PROJECT_SOURCE_DIR}/tools/channels/export_manifest.c"
        "${PROJECT_SOURCE_DIR}/src/support/json.c"
    INCLUDE_DIRECTORIES
        "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/tools/channels"
)
wm_add_test(wii-menu-prepare-update
    SOURCES
        tools/prepare_update_test.c
        "${PROJECT_SOURCE_DIR}/tools/preparation/prepare_fs.c"
        "${PROJECT_SOURCE_DIR}/tools/preparation/prepare_recovery.c"
        "${PROJECT_SOURCE_DIR}/tools/preparation/prepare_update.c"
        "${PROJECT_SOURCE_DIR}/src/support/json.c"
        "${PROJECT_SOURCE_DIR}/tools/wad/crypto.c"
    INCLUDE_DIRECTORIES
        "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/tools"
)
wm_add_test(wii-menu-settings-gif
    SOURCES
        tools/settings_gif_test.c
        "${PROJECT_SOURCE_DIR}/tools/settings/gif.c"
    LIBRARIES wii_menu_core
)
wm_add_test(wii-menu-settings-png
    SOURCES
        tools/settings_png_test.c
        "${PROJECT_SOURCE_DIR}/tools/settings/png.c"
    LIBRARIES wii_menu_core
)
wm_add_test(wii-menu-crypto
    SOURCES
        "${PROJECT_SOURCE_DIR}/tools/wad/crypto.c"
        tools/wad_crypto_test.c
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/tools/wad"
)
wm_add_test(wii-menu-wad-extract
    SOURCES
        tools/wad_extract_test.c
        "${PROJECT_SOURCE_DIR}/tools/wad/crypto.c"
        "${PROJECT_SOURCE_DIR}/tools/wad/retail_keys.c"
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/tools/wad"
)
wm_add_test(wii-menu-outline-font-export
    SOURCES tools/outline_font_export_test.c
    ARGUMENTS $<TARGET_FILE:wm-outline-font-export>
    DEPENDENCIES wm-outline-font-export
)
