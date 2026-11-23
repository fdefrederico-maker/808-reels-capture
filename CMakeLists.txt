cmake_minimum_required(VERSION 3.22)

# Tem de ser definido antes de project(): versão mínima do macOS suportada.
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Versão mínima do macOS")

project(ReelsCapture VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# O JUCE é descarregado automaticamente durante a configuração (não é preciso instalá-lo).
include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(juce)

add_subdirectory(plugin)
