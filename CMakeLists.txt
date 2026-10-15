cmake_minimum_required(VERSION 3.22)

project(ArtilodySoundsculp VERSION 1.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
endif()

# ---------------------------------------------------------------------------
# JUCE: use a local checkout if JUCE_DIR is given, otherwise download it.
# ---------------------------------------------------------------------------
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")

if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    # Populate manually and add with EXCLUDE_FROM_ALL so JUCE's own install rules
    # (headers, CMake files) do not end up in our installers.
    FetchContent_GetProperties(JUCE)
    if(NOT juce_POPULATED)
        FetchContent_Populate(JUCE)
        add_subdirectory(${juce_SOURCE_DIR} ${juce_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
endif()

juce_add_gui_app(ArtilodySoundsculp
    PRODUCT_NAME "Artilody Soundsculp"
    COMPANY_NAME "Artilody"
    BUNDLE_ID "com.artilody.soundsculp"
    VERSION ${PROJECT_VERSION}
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Artilody Soundsculp needs microphone access to record audio."
    DOCUMENT_EXTENSIONS wav flac ogg mp3 aiff aif m4a
)

juce_generate_juce_header(ArtilodySoundsculp)

juce_add_binary_data(SoundsculpData SOURCES
    Resources/logo_mark.png
    Resources/icon_256.png
)

target_sources(ArtilodySoundsculp PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/AudioDocument.cpp
    Source/AudioEngine.cpp
    Source/DSP.cpp
    Source/FileIO.cpp
    Source/WaveformView.cpp
    Source/EQPanel.cpp
    Source/NoisePanel.cpp
    Source/SidePanels.cpp
    Source/HeaderBar.cpp
    Source/Theme.cpp
    Source/TrackStack.cpp
    Source/ExportDialog.cpp
)

target_compile_definitions(ArtilodySoundsculp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ArtilodySoundsculp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ArtilodySoundsculp,JUCE_VERSION>"
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(ArtilodySoundsculp
    PRIVATE
        SoundsculpData
        juce::juce_gui_extra
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Installation / packaging (Linux .deb via CPack)
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(PROGRAMS "$<TARGET_FILE:ArtilodySoundsculp>"
            DESTINATION lib/artilody-soundsculp
            RENAME artilody-soundsculp)
    install(FILES packaging/linux/artilody-soundsculp.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    # app icon in several sizes so every menu/launcher finds a fitting one
    foreach(_size 48 64 128 256)
        install(FILES Resources/icon_${_size}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/apps
                RENAME com.artilody.soundsculp.png)
    endforeach()
    install(FILES Resources/icon.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps
            RENAME com.artilody.soundsculp.png)
    install(PROGRAMS packaging/linux/artilody-soundsculp
            DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY docs/pdf/ DESTINATION ${CMAKE_INSTALL_DOCDIR} OPTIONAL)

    set(CPACK_GENERATOR "DEB")
    set(CPACK_STRIP_FILES ON)
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/postrm")
    set(CPACK_PACKAGE_NAME "artilody-soundsculp")
    set(CPACK_PACKAGE_VENDOR "Artilody")
    set(CPACK_PACKAGE_CONTACT "Artilody <shenphenchoedron-hue@users.noreply.github.com>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Artilody Soundsculp - simple audio editor")
    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Simple multi-file audio editor with trim, cut/paste, fades,\n normalize, silence trim, loop, reverse, pitch shift, parametric EQ,\n noise remover, volume envelopes, text markers and export to many formats.")
    set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libasound2 | libasound2t64, libfreetype6, libfontconfig1, libx11-6, libxext6, libxrandr2, libxinerama1, libxcursor1, libgl1")
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "ffmpeg")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/shenphenchoedron-hue/Artilody-Soundsculp")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    if(NOT CPACK_DEBIAN_FILE_NAME)
        set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
    endif()
    include(CPack)
endif()
