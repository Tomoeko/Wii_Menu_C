add_executable(wm-wad-extract
    "${PROJECT_SOURCE_DIR}/src/support/sha1.c"
    wad/crypto.c
    wad/extract.c
    wad/retail_keys.c
)
target_include_directories(wm-wad-extract PRIVATE "${PROJECT_SOURCE_DIR}/include")
wm_enable_warnings(wm-wad-extract ERROR)

add_executable(wm-nand-extract
    "${PROJECT_SOURCE_DIR}/src/support/sha1.c"
    wad/crypto.c
    nand/reader.c
    nand/reader_discovery.c
    nand/reader_extract.c
    nand/extract.c
)
target_include_directories(wm-nand-extract PRIVATE "${PROJECT_SOURCE_DIR}/include")
wm_enable_warnings(wm-nand-extract ERROR)

add_executable(wm-prepare
    "${PROJECT_SOURCE_DIR}/src/support/asset_manifest.c"
    "${PROJECT_SOURCE_DIR}/src/support/sha1.c"
    preparation/prepare.c
    preparation/prepare_fs.c
    preparation/prepare_recovery.c
    preparation/prepare_update.c
    wad/crypto.c
    "${PROJECT_SOURCE_DIR}/src/support/json.c"
)
target_include_directories(wm-prepare PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
wm_enable_warnings(wm-prepare ERROR)

add_library(wm-export-directory STATIC shared/export_directory.c)
target_include_directories(wm-export-directory PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/shared"
)
wm_enable_warnings(wm-export-directory CONVERSION ERROR)

add_executable(wm-layout-export layout/export.c)
target_link_libraries(wm-layout-export PRIVATE wii_menu_core wm-export-directory)
target_include_directories(wm-layout-export PRIVATE
    "${PROJECT_SOURCE_DIR}/src/support"
)

add_executable(wm-keyboard-dictionary-export keyboard_dictionary/export.c)
target_link_libraries(wm-keyboard-dictionary-export PRIVATE
    wii_menu_core wm-export-directory
)
target_include_directories(wm-keyboard-dictionary-export PRIVATE
    "${PROJECT_SOURCE_DIR}/src/support"
)
wm_enable_warnings(wm-keyboard-dictionary-export CONVERSION ERROR)

add_executable(wm-settings-export
    settings/export.c
    settings/gif.c
    settings/png.c
)
target_link_libraries(wm-settings-export PRIVATE
    wii_menu_core wm-export-directory
)
wm_enable_warnings(wm-settings-export CONVERSION ERROR)

add_executable(wm-outline-font-export
    settings/outline_font_export.c
    "${PROJECT_SOURCE_DIR}/src/fonts/outline_font.c"
)
target_compile_definitions(wm-outline-font-export PRIVATE
    WM_OUTLINE_FONT_PARSE_ONLY)
target_link_libraries(wm-outline-font-export PRIVATE
    wii_menu_core wm-export-directory m
)

add_executable(wm-restart-export restart/export.c)
target_link_libraries(wm-restart-export PRIVATE
    wii_menu_core wm-export-directory
)
target_include_directories(wm-restart-export PRIVATE
    "${PROJECT_SOURCE_DIR}/src/support"
)

add_executable(wm-audio-export audio/export.c)
target_link_libraries(wm-audio-export PRIVATE
    wii_menu_core wm-export-directory m
)
target_include_directories(wm-audio-export PRIVATE
    "${PROJECT_SOURCE_DIR}/src/support"
)
wm_enable_warnings(wm-audio-export CONVERSION ERROR)

add_executable(wm-shared-font-export fonts/shared_font_export.c)
target_link_libraries(wm-shared-font-export PRIVATE wii_menu_core m)

add_executable(wm-channel-export
    "${PROJECT_SOURCE_DIR}/src/support/sha1.c"
    channels/export.c
    channels/export_resource.c
    channels/export_manifest.c
    channels/md5.c
    wad/crypto.c
)
target_include_directories(wm-channel-export PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(wm-channel-export PRIVATE wii_menu_core m)
target_include_directories(wm-channel-export PRIVATE
    "${PROJECT_SOURCE_DIR}/src/support"
)
wm_enable_warnings(wm-channel-export ERROR)

add_executable(wm-channels
    channels/manager.c
    channels/manager_delete.c
    channels/manager_import.c
    channels/manager_package.c
    "${PROJECT_SOURCE_DIR}/src/app/asset_path.c"
    preparation/prepare_fs.c
    settings/png.c
)
target_include_directories(wm-channels PRIVATE
    "${PROJECT_SOURCE_DIR}/src/app"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/settings"
)
target_link_libraries(wm-channels PRIVATE wii_menu_core)
add_dependencies(wm-channels wm-wad-extract wm-channel-export)
wm_enable_warnings(wm-channels ERROR)

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
