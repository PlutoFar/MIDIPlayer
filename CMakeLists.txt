cmake_minimum_required(VERSION 3.15)

project(ModernMidiPlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# JUCE Configuration
# ============================================================================
# Check if JUCE subdirectory exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(STATUS "Using JUCE from subdirectory")
    add_subdirectory(JUCE)
else()
    message(FATAL_ERROR 
        "JUCE not found! Please run:\n"
        "  git clone https://github.com/juce-framework/JUCE.git\n"
        "in the project directory."
    )
endif()

# ============================================================================
# Application Target
# ============================================================================
juce_add_gui_app(ModernMidiPlayer
    PRODUCT_NAME "Modern MIDI Player"
    VERSION "1.0.0"
    COMPANY_NAME "AntigravityAudio"
    BUNDLE_ID "com.antigravity.modernmidiplayer"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
)

# ============================================================================
# Source Files
# ============================================================================
file(GLOB_RECURSE SourceFiles 
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp" 
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h"
)

target_sources(ModernMidiPlayer PRIVATE
    ${SourceFiles}
)

target_include_directories(ModernMidiPlayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

# ============================================================================
# Compile Definitions
# ============================================================================
target_compile_definitions(ModernMidiPlayer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_PLUGINHOST_VST3=1
    # ASIO Support (requires ASIO SDK)
    JUCE_ASIO=1
    # Enable Unicode for Windows
    UNICODE=1
    _UNICODE=1
)

# ============================================================================
# Compiler Flags
# ============================================================================
if(MSVC)
    # Use UTF-8 source encoding
    target_compile_options(ModernMidiPlayer PRIVATE /utf-8)
    # Disable some warnings
    target_compile_options(ModernMidiPlayer PRIVATE /wd4100 /wd4244 /wd4267)
endif()

# ============================================================================
# Link Libraries
# ============================================================================
target_link_libraries(ModernMidiPlayer PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_data_structures
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ============================================================================
# Windows Specific
# ============================================================================
if(WIN32)
    target_link_libraries(ModernMidiPlayer PRIVATE dwmapi advapi32 shell32)
    
    # Set Windows subsystem
    set_target_properties(ModernMidiPlayer PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# ============================================================================
# Installation (optional)
# ============================================================================
install(TARGETS ModernMidiPlayer
    RUNTIME DESTINATION bin
    RESOURCE DESTINATION bin
)
