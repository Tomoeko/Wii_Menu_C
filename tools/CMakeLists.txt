add_executable(wm-wad-extract
    wad/crypto.c
    wad/extract.c
    wad/retail_keys.c
)
wm_enable_warnings(wm-wad-extract ERROR)

add_executable(wm-nand-extract
    wad/crypto.c
    nand/reader.c
    nand/reader_extract.c
    nand/extract.c
)
wm_enable_warnings(wm-nand-extract ERROR)

add_executable(wm-prepare
    preparation/prepare.c
    preparation/prepare_fs.c
    preparation/prepare_recovery.c
    wad/crypto.c
    "${PROJECT_SOURCE_DIR}/src/support/json.c"
)
target_include_directories(wm-prepare PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
wm_enable_warnings(wm-prepare ERROR)

add_executable(wm-layout-export layout/export.c)
target_link_libraries(wm-layout-export PRIVATE wii_menu_core)

add_executable(wm-keyboard-dictionary-export keyboard_dictionary/export.c)
target_link_libraries(wm-keyboard-dictionary-export PRIVATE wii_menu_core)
wm_enable_warnings(wm-keyboard-dictionary-export CONVERSION ERROR)

add_executable(wm-settings-export
    settings/export.c
    settings/gif.c
    settings/png.c
)
target_link_libraries(wm-settings-export PRIVATE wii_menu_core)
wm_enable_warnings(wm-settings-export CONVERSION ERROR)

add_executable(wm-outline-font-export
    settings/outline_font_export.c
    "${PROJECT_SOURCE_DIR}/src/fonts/outline_font.c"
)
target_compile_definitions(wm-outline-font-export PRIVATE
    WM_OUTLINE_FONT_PARSE_ONLY)
target_link_libraries(wm-outline-font-export PRIVATE wii_menu_core m)

add_executable(wm-restart-export restart/export.c)
target_link_libraries(wm-restart-export PRIVATE wii_menu_core)

add_executable(wm-audio-export audio/export.c)
target_link_libraries(wm-audio-export PRIVATE wii_menu_core m)
wm_enable_warnings(wm-audio-export CONVERSION ERROR)

add_executable(wm-shared-font-export fonts/shared_font_export.c)
target_link_libraries(wm-shared-font-export PRIVATE wii_menu_core m)

add_executable(wm-channel-export
    channels/export.c
    channels/export_manifest.c
    channels/md5.c
    wad/crypto.c
)
target_link_libraries(wm-channel-export PRIVATE wii_menu_core m)
wm_enable_warnings(wm-channel-export ERROR)

# Preparing assets invokes these tools next to the wm-prepare executable.
add_dependencies(wm-prepare
    wm-wad-extract
    wm-nand-extract
    wm-layout-export
    wm-keyboard-dictionary-export
    wm-settings-export
    wm-outline-font-export
    wm-restart-export
    wm-audio-export
    wm-shared-font-export
    wm-channel-export
)
