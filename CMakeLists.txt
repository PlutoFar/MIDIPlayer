cmake_minimum_required(VERSION 3.27)

project(ModernMidiPlayer VERSION 1.2.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_DIR "" CACHE PATH "Path to a JUCE source checkout")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(STATUS "Using JUCE from subdirectory")
    add_subdirectory(JUCE)
elseif(JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(STATUS "Using JUCE from ${JUCE_DIR}")
    add_subdirectory("${JUCE_DIR}" JUCE)
else()
    message(FATAL_ERROR
        "JUCE not found. Add a JUCE subdirectory or configure with "
        "-DJUCE_DIR=<path-to-JUCE>."
    )
endif()

option(MIDIPLAYER_ENABLE_ASIO
    "Enable ASIO support using the SDK bundled with JUCE" OFF)

# ============================================================================
# MidiCore - application state, commands, audio, playlist and export services.
# Shared by the JUCE desktop application, plugin worker and native tests.
# ============================================================================
add_library(MidiCore STATIC
    Source/Utils/WindowsFileAssociation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/Core.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/AudioDevices.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/Library.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/Player.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/Playlist.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/ExportTask.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/AudioEngine/AudioEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/AudioEngine/AudioThreadPriority.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/AudioEngine/AudioDeviceService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/AudioEngine/PluginLibrary.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/AudioEngine/OfflineRenderer.cpp
)

target_include_directories(MidiCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_compile_definitions(MidiCore PUBLIC
    MIDI_PLAYER_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_ASIO=$<BOOL:${MIDIPLAYER_ENABLE_ASIO}>
    UNICODE=1
    _UNICODE=1
)

target_link_libraries(MidiCore PUBLIC
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_data_structures
    juce::juce_events
    juce::juce_recommended_config_flags
)

if(MSVC)
    target_compile_options(MidiCore PRIVATE /utf-8)
    target_compile_options(MidiCore PRIVATE /wd4100 /wd4244 /wd4267)
endif()

# ============================================================================
# MidiPlayer - JUCE desktop application, using MidiCore.
# Produces MidiPlayer.exe beside the matching MidiWorker.exe.
# ============================================================================
juce_add_gui_app(MidiPlayer
    PRODUCT_NAME "MidiPlayer"
    VERSION "${PROJECT_VERSION}"
    COMPANY_NAME "AntigravityAudio"
    BUNDLE_ID "com.antigravity.modernmidiplayer"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
)

# Each executable owns its entry point; shared .cpp files belong to MidiCore.
target_sources(MidiPlayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/MainContentComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/MainContentPlugins.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/MainContentFiles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/MainContentSettings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/AudioSettingsContent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/FontSettingsContent.cpp
)

target_link_libraries(MidiPlayer PRIVATE
    MidiCore
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if(MSVC)
    target_compile_options(MidiPlayer PRIVATE /utf-8)
    target_compile_options(MidiPlayer PRIVATE /wd4100 /wd4244 /wd4267)
endif()

# ============================================================================
# MidiWorker - VST3 plugin worker child process. Produces MidiWorker.exe.
# Launched by the desktop application via the worker command-line UID.
# ============================================================================
juce_add_gui_app(MidiWorker
    PRODUCT_NAME "MidiWorker"
    VERSION "${PROJECT_VERSION}"
    COMPANY_NAME "AntigravityAudio"
    BUNDLE_ID "com.antigravity.midiworker"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/worker-icon.png"
)

target_sources(MidiWorker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Worker/Main.cpp
)

target_link_libraries(MidiWorker PRIVATE
    MidiCore
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if(MSVC)
    target_compile_options(MidiWorker PRIVATE /utf-8)
    target_compile_options(MidiWorker PRIVATE /wd4100 /wd4244 /wd4267)
endif()

add_dependencies(MidiPlayer MidiWorker)
add_custom_command(TARGET MidiPlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:MidiWorker>
        $<TARGET_FILE_DIR:MidiPlayer>
    COMMENT "Copying the matching plugin worker beside MidiPlayer"
)

# ============================================================================
# MidiTests - non-UI test runner. Excluded from the default build.
# ============================================================================
juce_add_console_app(MidiTests
    PRODUCT_NAME "MidiTests"
)

set_target_properties(MidiTests MidiTests_rc_lib PROPERTIES
    EXCLUDE_FROM_ALL TRUE
    EXCLUDE_FROM_DEFAULT_BUILD TRUE
)

target_sources(MidiTests PRIVATE
    Tests/ExportAndHintTests.cpp
    Tests/WorkerPathTests.cpp
    Tests/CoreTests.cpp
    Tests/ExportTaskTests.cpp
    Tests/PersistenceTests.cpp
)

target_link_libraries(MidiTests PRIVATE
    MidiCore
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_warning_flags
)

if(MSVC)
    target_compile_options(MidiTests PRIVATE /utf-8)
endif()

if(WIN32)
    target_link_libraries(MidiCore PUBLIC avrt advapi32 shell32 shlwapi)

    # Ordering: JUCE's resource rule must rerun when version metadata changes.
    foreach(app IN ITEMS MidiPlayer MidiWorker)
        get_target_property(resource_dir ${app} JUCE_GENERATED_SOURCES_DIRECTORY)
        get_target_property(resource_info ${app} JUCE_INFO_FILE)
        add_custom_command(OUTPUT "${resource_dir}/${app}_resources.rc"
            APPEND DEPENDS "${resource_info}")
    endforeach()

    target_link_libraries(MidiPlayer PRIVATE dwmapi advapi32 shell32 kernel32)

    set_target_properties(MidiPlayer PROPERTIES
        WIN32_EXECUTABLE TRUE
    )

    set_target_properties(MidiWorker PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

install(TARGETS MidiPlayer MidiWorker
    RUNTIME DESTINATION bin
    RESOURCE DESTINATION bin
)
