if(WM_BACKEND STREQUAL "metal")
    if(NOT APPLE)
        message(FATAL_ERROR "Metal requires an Apple platform.")
    endif()
    enable_language(OBJC)
    target_sources(wii-menu PRIVATE
        platform/metal/platform_metal.m
        platform/metal/platform_metal_window.m
        platform/metal/shaders.m
        platform/apple/audio_platform_apple.c
    )
    set_source_files_properties(
        platform/metal/platform_metal.m
        platform/metal/platform_metal_window.m
        platform/metal/shaders.m
        PROPERTIES
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
        platform/gles2/retained_frame.c
        platform/gles2/host.c
        platform/gles2/shaders.c
        platform/linux/audio_platform_linux.c
    )
    target_link_libraries(wii-menu PRIVATE
        X11::X11 ${EGL_LIBRARY} ${GLES2_LIBRARY} m dl)
else()
    message(FATAL_ERROR "WM_BACKEND must be metal or gles2.")
endif()
