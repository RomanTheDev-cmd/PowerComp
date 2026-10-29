cmake_minimum_required(VERSION 3.22)

# Must come before project(): set later, it was ignored and the builds required the build machine's
# macOS (27.0), so they would not load on older Macs (found 2026-10-23 in PowerKick). arm64 slices start at 11.0 anyway.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS")

project(PowerComp VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary" FORCE)
endif()

option(POWERCOMP_COPY_AFTER_BUILD "Install plug-ins into the user plug-in folders after build" OFF)

# cmake/patch_juce.cmake fixes JUCE before it is used (Windows: closing the editor could hang for ever).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
  set(JUCE_PATCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE")
  include(cmake/patch_juce.cmake)
  add_subdirectory(third_party/JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
                            GIT_TAG 29396c22c93392d6738e021b83196283d6e4d850
                            PATCH_COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_juce.cmake")
  FetchContent_MakeAvailable(JUCE)
endif()

# Cross-compiling for Windows: the manifest helper needs an emulator (Wine) to run here.
if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
  set(POWER_VST3_AUTO_MANIFEST FALSE)
else()
  set(POWER_VST3_AUTO_MANIFEST TRUE)
endif()

# An effect: no standalone application (it would only process the microphone).
set(POWERCOMP_FORMATS VST3)
if(APPLE)
  list(APPEND POWERCOMP_FORMATS AU)
endif()

juce_add_plugin(PowerComp
  COMPANY_NAME "MyDarknessDays"
  COMPANY_WEBSITE ""
  BUNDLE_ID "com.mydarknessdays.powercomp"
  PLUGIN_MANUFACTURER_CODE Mddy
  PLUGIN_CODE Pwcp
  PRODUCT_NAME "PowerComp"
  FORMATS ${POWERCOMP_FORMATS}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  VST3_CATEGORIES Fx Dynamics
  AU_MAIN_TYPE kAudioUnitType_Effect
  VST3_AUTO_MANIFEST ${POWER_VST3_AUTO_MANIFEST}
  COPY_PLUGIN_AFTER_BUILD ${POWERCOMP_COPY_AFTER_BUILD})

set(POWERCOMP_CORE_SOURCES
  Source/Params.cpp
  Source/Presets.cpp
)

target_sources(PowerComp PRIVATE
  ${POWERCOMP_CORE_SOURCES}
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

target_include_directories(PowerComp PRIVATE Source)
target_compile_definitions(PowerComp PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(PowerComp
  PRIVATE juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Headless DSP/preset tests: the same engine code, no plug-in wrapper.
juce_add_console_app(powercomp_tests PRODUCT_NAME "PowerComp tests")
target_sources(powercomp_tests PRIVATE ${POWERCOMP_CORE_SOURCES} Tests/PowerCompTests.cpp)
target_include_directories(powercomp_tests PRIVATE Source)
target_compile_definitions(powercomp_tests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(powercomp_tests
  PRIVATE juce::juce_audio_formats juce::juce_audio_processors juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME powercomp_tests COMMAND powercomp_tests)

# Wrapper tests: processor, state, presets through the host path and the editor, linked
# against the plug-in's shared code. (Shared code exports JUCE_STANDALONE_APPLICATION=0.)
juce_add_console_app(powercomp_wrapper_tests PRODUCT_NAME "PowerComp wrapper tests")
get_target_property(POWERCOMP_WRAP_DEFS powercomp_wrapper_tests COMPILE_DEFINITIONS)
list(REMOVE_ITEM POWERCOMP_WRAP_DEFS "JUCE_STANDALONE_APPLICATION=1")
set_target_properties(powercomp_wrapper_tests PROPERTIES COMPILE_DEFINITIONS "${POWERCOMP_WRAP_DEFS}")
target_sources(powercomp_wrapper_tests PRIVATE Tests/WrapperTests.cpp)
target_include_directories(powercomp_wrapper_tests PRIVATE Source)
target_link_libraries(powercomp_wrapper_tests PRIVATE PowerComp juce::juce_audio_utils)
add_test(NAME powercomp_wrapper_tests COMMAND powercomp_wrapper_tests)
