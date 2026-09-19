cmake_minimum_required(VERSION 3.15)

# Project name and version
project(FreakPhase VERSION 1.0.0 LANGUAGES CXX C)

# Set C++ standard to C++17 (JUCE 6+ requires C++17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform detection
if(WIN32)
    add_definitions(-D_WIN32)
    if(MSVC)
        add_definitions(-D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /EHsc /MP")
    endif()
elseif(APPLE)
    add_definitions(-D__MACOSX__)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
elseif(UNIX AND NOT APPLE)
    add_definitions(-D__LINUX__)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wshadow -Wconversion")
endif()

# JUCE configuration
set(JUCE_MODULES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/modules" CACHE PATH "Path to JUCE modules")

# Enable VST3 format
option(JUCE_PLUGINHOST_VST3 "Enable VST3 format" ON)
option(JUCE_PLUGINHOST_AU "Enable Audio Units format" OFF)
option(JUCE_PLUGINHOST_VST "Enable VST2 format" OFF)
option(BUILD_STANDALONE "Build standalone application" OFF)

# Find JUCE (if installed system-wide)
find_package(JUCE CONFIG QUIET)
if(NOT JUCE_FOUND)
    # Use local JUCE modules path
    set(JUCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to JUCE library")
    if(NOT EXISTS "${JUCE_ROOT}/modules/juce_core/juce_core.h")
        message(WARNING "JUCE not found at ${JUCE_ROOT}. Please set JUCE_ROOT to your JUCE installation path.")
    endif()
endif()

# Add JUCE modules
if(JUCE_FOUND)
    include(${JUCE_CONFIG})
else()
    # Manual JUCE module setup
    set(JUCE_MODULE_AVAILABLE_juce_core ON)
    set(JUCE_MODULE_AVAILABLE_juce_events ON)
    set(JUCE_MODULE_AVAILABLE_juce_audio_basics ON)
    set(JUCE_MODULE_AVAILABLE_juce_audio_devices ON)
    set(JUCE_MODULE_AVAILABLE_juce_audio_formats ON)
    set(JUCE_MODULE_AVAILABLE_juce_audio_processors ON)
    set(JUCE_MODULE_AVAILABLE_juce_audio_utils ON)
    set(JUCE_MODULE_AVAILABLE_juce_data_structures ON)
    set(JUCE_MODULE_AVAILABLE_juce_graphics ON)
    set(JUCE_MODULE_AVAILABLE_juce_gui_basics ON)
    set(JUCE_MODULE_AVAILABLE_juce_gui_extra ON)
    set(JUCE_MODULE_AVAILABLE_juce_video ON)
    set(JUCE_MODULE_AVAILABLE_juce_analytics ON)
    set(JUCE_MODULE_AVAILABLE_juce_animation ON)
    set(JUCE_MODULE_AVAILABLE_juce_product_unlocking ON)
    set(JUCE_MODULE_AVAILABLE_juce_osc ON)
    
    # Add JUCE include directories
    include_directories(
        ${JUCE_ROOT}/modules
        ${JUCE_ROOT}/modules/juce_core
        ${JUCE_ROOT}/modules/juce_events
        ${JUCE_ROOT}/modules/juce_audio_basics
        ${JUCE_ROOT}/modules/juce_audio_devices
        ${JUCE_ROOT}/modules/juce_audio_formats
        ${JUCE_ROOT}/modules/juce_audio_processors
        ${JUCE_ROOT}/modules/juce_audio_utils
        ${JUCE_ROOT}/modules/juce_data_structures
        ${JUCE_ROOT}/modules/juce_graphics
        ${JUCE_ROOT}/modules/juce_gui_basics
        ${JUCE_ROOT}/modules/juce_gui_extra
    )
endif()

# Source files
set(SOURCE_FILES
    PluginProcessor.cpp
    PluginEditor.cpp
    
    Core/ParameterCache.h
    Core/PresetManager.h
    
    DSP/PhaseEngine.cpp
    DSP/PhaseEngine.h
    DSP/DynamicEq.cpp
    DSP/DynamicEq.h
    DSP/EnvelopeFollower.cpp
    DSP/EnvelopeFollower.h
    DSP/AutoAlignerThread.cpp
    DSP/AutoAlignerThread.h
    DSP/DcBlockerChain.h
    DSP/SubCrossover.h
    DSP/PitchTracker.h
    DSP/SimdMath.h
    
    Analysis/SignalAnalyzer.cpp
    Analysis/SignalAnalyzer.h
    
    Timeline/PlayheadTracker.h
    Timeline/BakedTimelineLUT.h
    Timeline/TimelineAudioCaptureFifo.h
    Timeline/TimelineAnalysisThread.h
)

# Include directories
include_directories(
    .
    Core
    DSP
    Analysis
    Timeline
    Source/UI
    UI/WebUI
)

# Resource files (WebUI)
set(RESOURCE_FILES
    UI/WebUI/index.html
    UI/WebUI/assets/index-CwNuoDAa.js
    UI/WebUI/assets/index-CjlAcj4F.css
    UI/WebUI/assets/JetBrainsMono-greek-Bw9x6K1M.woff2
    UI/WebUI/assets/JetBrainsMono-latin-B9CIFXIH.woff2
)

# Create plugin target
if(JUCE_PLUGINHOST_VST3)
    add_library(FreakPhase_VST3 MODULE ${SOURCE_FILES} ${RESOURCE_FILES})
    target_compile_definitions(FreakPhase_VST3 PRIVATE JUCE_VST3=1)
    
    # VST3 specific settings
    target_compile_definitions(FreakPhase_VST3 PRIVATE 
        JucePlugin_Build_VST3=1
        JucePlugin_VST3Category="Delay|Fx"
        JucePlugin_Manufacturer="zibi.pr0d"
        JucePlugin_ManufacturerCode="Zbpl"
        JucePlugin_PluginCode="PhaS"
        JucePlugin_PluginName="FreakPhase"
        JucePlugin_Version="1.0.0"
    )
    
    # Windows VST3 output directory
    if(WIN32)
        set_target_properties(FreakPhase_VST3 PROPERTIES
            PREFIX ""
            SUFFIX ".vst3"
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Builds/VST3
        )
    endif()
endif()

# Create standalone target if requested
if(BUILD_STANDALONE)
    add_executable(FreakPhase_Standalone ${SOURCE_FILES} ${RESOURCE_FILES})
    target_compile_definitions(FreakPhase_Standalone PRIVATE JUCE_STANDALONE=1)
    
    if(WIN32)
        target_compile_definitions(FreakPhase_Standalone PRIVATE JucePlugin_Build_Standalone=1)
        set_target_properties(FreakPhase_Standalone PROPERTIES
            OUTPUT_NAME "FreakPhase"
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Builds/Standalone
        )
    endif()
endif()

# Link JUCE libraries (if using system JUCE)
if(JUCE_FOUND)
    target_link_libraries(FreakPhase_VST3 PRIVATE JUCE::juce_core JUCE::juce_events 
        JUCE::juce_audio_basics JUCE::juce_audio_devices JUCE::juce_audio_formats 
        JUCE::juce_audio_processors JUCE::juce_audio_utils JUCE::juce_data_structures 
        JUCE::juce_graphics JUCE::juce_gui_basics JUCE::juce_gui_extra)
    
    if(BUILD_STANDALONE)
        target_link_libraries(FreakPhase_Standalone PRIVATE JUCE::juce_core JUCE::juce_events 
            JUCE::juce_audio_basics JUCE::juce_audio_devices JUCE::juce_audio_formats 
            JUCE::juce_audio_processors JUCE::juce_audio_utils JUCE::juce_data_structures 
            JUCE::juce_graphics JUCE::juce_gui_basics JUCE::juce_gui_extra)
    endif()
endif()

# Platform-specific settings
if(WIN32)
    # Windows: Enable multi-processor compilation
    if(MSVC)
        add_compile_options(/MP)
    endif()
    
    # Set output directory for all targets
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Builds)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Builds)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Builds)
    
    # VST3 plugin should be a DLL on Windows
    if(JUCE_PLUGINHOST_VST3)
        set_target_properties(FreakPhase_VST3 PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
    endif()
    
    # Copy WebUI files to build directory
    add_custom_command(TARGET FreakPhase_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/UI/WebUI
            $<TARGET_FILE_DIR:FreakPhase_VST3>/Resources
        COMMENT "Copying WebUI files to VST3 bundle"
    )
    
    if(BUILD_STANDALONE)
        add_custom_command(TARGET FreakPhase_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/UI/WebUI
                $<TARGET_FILE_DIR:FreakPhase_Standalone>/Resources
            COMMENT "Copying WebUI files to standalone bundle"
        )
    endif()
    
elseif(APPLE)
    # macOS settings
    set(CMAKE_MACOSX_BUNDLE ON)
    set(CMAKE_MACOSX_RPATH ON)
    
    if(JUCE_PLUGINHOST_VST3)
        set_target_properties(FreakPhase_VST3 PROPERTIES
            MACOSX_BUNDLE ON
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Builds/VST3
        )
    endif()
    
    # Copy WebUI files for macOS
    if(JUCE_PLUGINHOST_VST3)
        add_custom_command(TARGET FreakPhase_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/UI/WebUI
                $<TARGET_BUNDLE_CONTENT_DIR:FreakPhase_VST3>/Resources
            COMMENT "Copying WebUI files to VST3 bundle"
        )
    endif()
    
elseif(UNIX AND NOT APPLE)
    # Linux settings
    if(JUCE_PLUGINHOST_VST3)
        set_target_properties(FreakPhase_VST3 PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Builds/VST3
        )
    endif()
endif()

# Installation targets
if(JUCE_PLUGINHOST_VST3)
    install(TARGETS FreakPhase_VST3
        LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}/VST3"
    )
    install(DIRECTORY UI/WebUI
        DESTINATION "${CMAKE_INSTALL_PREFIX}/VST3/FreakPhase.vst3/Contents/Resources"
    )
endif()

if(BUILD_STANDALONE)
    install(TARGETS FreakPhase_Standalone
        RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/Standalone"
    )
    install(DIRECTORY UI/WebUI
        DESTINATION "${CMAKE_INSTALL_PREFIX}/Standalone/FreakPhase.app/Contents/Resources"
    )
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "=== FreakPhase Build Configuration ===")
message(STATUS "Project Version: ${PROJECT_VERSION}")
message(STATUS "C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "VST3 Format: ${JUCE_PLUGINHOST_VST3}")
message(STATUS "Standalone: ${BUILD_STANDALONE}")
message(STATUS "Build Directory: ${CMAKE_BINARY_DIR}")
message(STATUS "")
