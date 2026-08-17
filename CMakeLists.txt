cmake_minimum_required(VERSION 3.27)

project(ModernMidiPlayer VERSION 1.0.0 LANGUAGES C CXX)

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
    "Enable JUCE ASIO support (requires the Steinberg ASIO SDK)" OFF)

# ============================================================================
# MidiCore - framework-agnostic application core (state + commands).
#
# A STATIC library so it can be consumed by BOTH the CMake world (Legacy,
# Worker, Tests) and the WinUI 3 MSBuild project (which links the produced
# MidiCore.lib). It carries the shared include path, JUCE audio/data
# dependencies and compile definitions for every native target.
#
# MidiCore compiles the shared Core translation units and exposes the audio,
# playlist, export, and plugin-worker services used by both front ends.
# ============================================================================
add_library(MidiCore STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/Core.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/LegacyBridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/Library.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/Player.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/Playlist.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/ExportTask.cpp
)

target_include_directories(MidiCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_compile_definitions(MidiCore PUBLIC
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
# MidiWinBridge - STL-only WinUI adapter over MidiCore.
# Kept outside MidiCore so the shared core library does not carry front-end
# bridge responsibilities.
# ============================================================================
add_library(MidiWinBridge STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/WinBridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/WinUiSettingsAdapter.cpp
)

target_link_libraries(MidiWinBridge PUBLIC MidiCore)

if(MSVC)
    target_compile_options(MidiWinBridge PRIVATE /utf-8)
    target_compile_options(MidiWinBridge PRIVATE /wd4100 /wd4244 /wd4267)
endif()

# ============================================================================
# MidiLegacy - the existing JUCE UI, running on top of MidiCore.
# Fallback build; produces MidiLegacy.exe.
# ============================================================================
juce_add_gui_app(MidiLegacy
    PRODUCT_NAME "MidiLegacy"
    VERSION "1.0.0"
    COMPANY_NAME "AntigravityAudio"
    BUNDLE_ID "com.antigravity.modernmidiplayer"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
)

file(GLOB_RECURSE LegacySources
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h"
)

# The worker has its own entry point (START_JUCE_APPLICATION); keep it out of
# the Legacy app to avoid a duplicate main. Core .cpp units are compiled into
# MidiCore, so exclude them here to avoid duplicate symbols. The WinUI front-end
# is a separate C++/WinRT MSBuild project, not part of the JUCE build.
list(FILTER LegacySources EXCLUDE REGEX "/Source/Worker/")
list(FILTER LegacySources EXCLUDE REGEX "/Source/Core/.*\\.cpp$")
list(FILTER LegacySources EXCLUDE REGEX "/Source/WinUI/")

target_sources(MidiLegacy PRIVATE
    ${LegacySources}
)

target_link_libraries(MidiLegacy PRIVATE
    MidiCore
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if(MSVC)
    target_compile_options(MidiLegacy PRIVATE /utf-8)
    target_compile_options(MidiLegacy PRIVATE /wd4100 /wd4244 /wd4267)
endif()

# ============================================================================
# MidiWorker - VST3 plugin worker child process. Produces MidiWorker.exe.
# Launched by the main app (WinUI or Legacy) via the worker command-line UID.
# ============================================================================
juce_add_gui_app(MidiWorker
    PRODUCT_NAME "MidiWorker"
    VERSION "1.0.0"
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
    Tests/WinBridgeTests.cpp
)

target_link_libraries(MidiTests PRIVATE
    MidiWinBridge
    MidiCore
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_warning_flags
)

if(MSVC)
    target_compile_options(MidiTests PRIVATE /utf-8)
endif()

if(WIN32)
    target_link_libraries(MidiLegacy PRIVATE dwmapi advapi32 shell32 kernel32)

    set_target_properties(MidiLegacy PROPERTIES
        WIN32_EXECUTABLE TRUE
    )

    set_target_properties(MidiWorker PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

install(TARGETS MidiLegacy
    RUNTIME DESTINATION bin
    RESOURCE DESTINATION bin
)
