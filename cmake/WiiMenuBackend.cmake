if(WM_BACKEND STREQUAL "metal")
    if(NOT APPLE)
        message(FATAL_ERROR "Metal requires an Apple platform.")
    endif()
    enable_language(OBJC)
    target_sources(wii-menu PRIVATE
        platform/metal/platform_metal.m
        platform/apple/audio_platform_apple.c
    )
    set_source_files_properties(platform/metal/platform_metal.m PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc")
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
    target_sources(wii-menu PRIVATE
        platform/gles2/platform_gles2.c
        platform/linux/audio_platform_linux.c
    )
    target_link_libraries(wii-menu PRIVATE
        X11::X11 ${EGL_LIBRARY} ${GLES2_LIBRARY} m dl)
elseif(WM_BACKEND STREQUAL "psvr2")
    if(NOT WM_PSVR2_BUILD_ENTRY)
        message(FATAL_ERROR
            "Build PSVR2 through the native build.sh wii-menu --project PATH entry point.")
    endif()
    if(NOT WM_PSVR2_SOURCE_FAMILY STREQUAL "0600")
        message(FATAL_ERROR "The Wii Menu PSVR2 adapter currently supports firmware 06.00.")
    endif()
    set(vrhmd "${WM_PSVR2_TARGET_ROOT}/tools/open_vrhmd")
    # The shared PSVR2 hardware layer is compiled as GNU C11 by build.sh.
    set_target_properties(wii-menu PROPERTIES C_EXTENSIONS ON SKIP_BUILD_RPATH ON)
    target_sources(wii-menu PRIVATE
        platform/gles2/platform_gles2.c
        platform/psvr2/vr_layout.c
        platform/psvr2/audio_platform_psvr2.c
        platform/psvr2/pointer_psvr2.c
        platform/psvr2/pointer_protocol.c
        "${vrhmd}/core/device.c"
        "${vrhmd}/core/devmem.c"
        "${vrhmd}/core/fan_ctrl.c"
        "${vrhmd}/display/display.c"
        "${vrhmd}/display/panel.c"
        "${vrhmd}/render/color.c"
    )
    target_include_directories(wii-menu PRIVATE "${vrhmd}")
    target_compile_definitions(wii-menu PRIVATE WM_PLATFORM_PSVR2=1
        PSVR2_SOURCE_FAMILY_0600=1)
    target_link_libraries(wii-menu PRIVATE
        "${WM_PSVR2_EGL_LIBRARY}" "${WM_PSVR2_GLES_LIBRARY}" m dl)
    target_link_options(wii-menu PRIVATE "-Wl,-rpath=/lib")
else()
    message(FATAL_ERROR "WM_BACKEND must be metal, gles2, or psvr2.")
endif()
