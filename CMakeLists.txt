cmake_minimum_required(VERSION 3.15)
project(SmartAgent)
set(TARGET smart-agent)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set CMake policies
cmake_policy(SET CMP0135 NEW)

# Silence OpenGL deprecation warnings on macOS
if(APPLE)
    add_definitions(-DGL_SILENCE_DEPRECATION)
endif()

# Find packages
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(CURL REQUIRED)

# Include nlohmann_json as a header-only library
include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

# ImGui source files
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# Add llama.cpp as a subdirectory to build its library
add_subdirectory(external/llama.cpp)

# Application source files
set(SOURCES
    main.cpp
    Application.cpp
    ./gui/OpenGLRenderer.cpp
    ./llm-interface/ModelInterface.cpp
    ./llm-interface/ModelManager.cpp
    ContextManager.cpp
    Logger.cpp
)

# Platform-specific dependencies
if(WIN32)
    set(PLATFORM_LIBS "")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    include_directories(${GTK3_INCLUDE_DIRS})
    link_directories(${GTK3_LIBRARY_DIRS})
    add_definitions(${GTK3_CFLAGS_OTHER})
    set(PLATFORM_LIBS ${GTK3_LIBRARIES})
endif()

# Create executable
add_executable(${TARGET} ${SOURCES} ${IMGUI_SOURCES})

# Include directories
target_include_directories(${TARGET} PRIVATE
    ./llm-interface
    ./gui
    ${OPENGL_INCLUDE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CURL_INCLUDE_DIRS}
)

# Installation
install(TARGETS ${TARGET} RUNTIME) 

# Link libraries
target_link_libraries(${TARGET} PRIVATE
    ${OPENGL_LIBRARIES}
    glfw
    ${PLATFORM_LIBS}
    ${CURL_LIBRARIES}
    nlohmann_json::nlohmann_json
    llama
    ${CMAKE_THREAD_LIBS_INIT}
)
