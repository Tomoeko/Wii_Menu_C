cc_add_platform(wii-menu "${WM_BACKEND}")
target_sources(wii-menu PRIVATE audio/audio_platform.c)
target_link_libraries(wii-menu PRIVATE console_common_audio)
