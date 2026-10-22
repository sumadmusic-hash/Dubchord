cmake_minimum_required(VERSION 3.20)
project(DubTechSynthCore LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_DUBPROTOTYPE_PLUGIN "Build the JUCE-based VST3 feasibility prototype (macOS/Apple Silicon only)" OFF)

add_library(dubcore STATIC
    src/core/ParamCatalog.cpp
    src/core/ParameterControlLayer.cpp
    src/core/PerformanceRecorder.cpp
    src/core/PresetStore.cpp
    src/core/VoicePool.cpp
)
target_include_directories(dubcore PUBLIC src/core)
target_compile_options(dubcore PRIVATE -Wall -Wextra)

enable_testing()
foreach(t test_params_and_control test_recorder test_preset_voice)
    add_executable(${t} tests/${t}.cpp)
    target_link_libraries(${t} PRIVATE dubcore)
    target_include_directories(${t} PRIVATE tests)
    target_compile_options(${t} PRIVATE -Wall -Wextra)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

find_package(Threads REQUIRED)
target_link_libraries(test_params_and_control PRIVATE Threads::Threads)

if(BUILD_DUBPROTOTYPE_PLUGIN)
    # Phase-0-Prototyp (Bauplan 18): nur auf macOS/Apple Silicon aktivierbar.
    # Bewusst ABHANGIGKEIT offen gelassen: JUCE-Checkout muss auf dem
    # Zielrechner mit gepruefter Lizenz erfolgen (Bauplan 13).
    if(NOT APPLE)
        message(FATAL_ERROR
          "BUILD_DUBPROTOTYPE_PLUGIN ist nur unter macOS sinnvoll (Ziel: Apple Silicon, Audiotool VST Bridge). "
          "In dieser Linux-Umgebung darf kein ungetestetes Plugin erzeugt werden.")
    endif()
    message(STATUS "Plugin-Ziel angefordert: JUCE + VST3-Setup gemaess docs/03_MACHBARKEIT_AUDIOTOOL_CHECKLISTE.md ergaenzen.")
endif()
