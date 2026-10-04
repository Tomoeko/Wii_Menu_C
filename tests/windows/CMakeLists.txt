# Native Windows checks reuse the portable mixer and reconstruction fixtures.
wm_add_test(wii-menu-app-options
    SOURCES ../app/app_options_test.c "${PROJECT_SOURCE_DIR}/src/app/app_options.c"
    LIBRARIES console_common
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/app")

wm_add_test(wii-menu-audio-platform
    SOURCES ../audio/audio_platform_test.c
        "${PROJECT_SOURCE_DIR}/src/audio/audio_platform.c"
    LIBRARIES console_common
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/audio")

foreach(module capture held held-runtime restart sequence wave)
    string(REPLACE "-" "_" source_name "${module}")
    wm_add_test(wii-menu-audio-${module}
        SOURCES "../audio/audio_${source_name}_test.c"
        LIBRARIES wii_menu_core
        INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/audio")
    set_tests_properties(wii-menu-audio-${module} PROPERTIES TIMEOUT 15)
endforeach()

wm_add_test(wii-menu-hover-audio
    SOURCES ../audio/hover_audio_test.c
    LIBRARIES wii_menu_core)

if(WM_BUILD_TOOLS)
    wm_add_test(wii-menu-export-directory
        SOURCES ../tools/export_directory_test.c
        LIBRARIES wm-export-directory wii_menu_core
        INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/support"
            "${PROJECT_SOURCE_DIR}/Common/src" "${PROJECT_SOURCE_DIR}/Common/tests")
    wm_add_test(wii-menu-prepare-update
        LIBRARIES console_common
        SOURCES
            ../tools/prepare_update_test.c
            "${PROJECT_SOURCE_DIR}/src/support/asset_manifest.c"
            "${PROJECT_SOURCE_DIR}/tools/preparation/prepare_fs.c"
            "${PROJECT_SOURCE_DIR}/tools/preparation/prepare_recovery.c"
            "${PROJECT_SOURCE_DIR}/tools/preparation/prepare_update.c"
            "${PROJECT_SOURCE_DIR}/tools/wad/crypto.c"
        INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/include"
            "${PROJECT_SOURCE_DIR}/tools")
    wm_add_test(wii-menu-wad-extract
        LIBRARIES console_common
        SOURCES ../tools/wad_extract_test.c
            "${PROJECT_SOURCE_DIR}/tools/wad/crypto.c"
            "${PROJECT_SOURCE_DIR}/tools/wad/retail_keys.c"
        INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/tools/wad")
    wm_add_test(wii-menu-crypto
        LIBRARIES console_common
        SOURCES ../tools/wad_crypto_test.c
            "${PROJECT_SOURCE_DIR}/tools/wad/crypto.c"
        INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/tools/wad")
    foreach(format gif png)
        wm_add_test(wii-menu-settings-${format}
            LIBRARIES wii_menu_core
            SOURCES "../tools/settings_${format}_test.c"
                "${PROJECT_SOURCE_DIR}/tools/settings/${format}.c")
    endforeach()
endif()

add_subdirectory("${PROJECT_SOURCE_DIR}/Common/tests" common-tests)
