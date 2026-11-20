cmake_minimum_required(VERSION 3.16)

project(limit-nvpstate LANGUAGES C CXX)

if(NOT MSVC)
    message(FATAL_ERROR "limit-nvpstate links against nvapi64.lib and must be built with the MSVC toolchain (Visual Studio Build Tools, no IDE required)")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Qt 5 (tested line: 5.14.2 / 5.15.2) or Qt 6
find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/limit-nvpstate)

add_executable(limit-nvpstate WIN32
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/about.cpp
    ${SRC_DIR}/about.h
    ${SRC_DIR}/about.ui
    ${SRC_DIR}/config.cpp
    ${SRC_DIR}/config.h
    ${SRC_DIR}/limitnvpstate.cpp
    ${SRC_DIR}/limitnvpstate.h
    ${SRC_DIR}/limitnvpstate.ui
    ${SRC_DIR}/limitnvpstate.qrc
    ${SRC_DIR}/nvidia.cpp
    ${SRC_DIR}/nvidia.h
    ${SRC_DIR}/ruledialog.cpp
    ${SRC_DIR}/ruledialog.h
    ${SRC_DIR}/rules.cpp
    ${SRC_DIR}/rules.h
    ${SRC_DIR}/utils.cpp
    ${SRC_DIR}/utils.h
    ${SRC_DIR}/resource.h
    ${SRC_DIR}/limit-nvpstate.rc
)

target_include_directories(limit-nvpstate PRIVATE
    ${SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/nvapi
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party # nlohmann/json.hpp (single header)
)

target_compile_definitions(limit-nvpstate PRIVATE UNICODE _UNICODE)
target_compile_options(limit-nvpstate PRIVATE /utf-8 /MP)

target_link_libraries(limit-nvpstate PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/nvapi/amd64/nvapi64.lib
    Shlwapi.lib
    Kernel32.lib
    User32.lib
)

target_link_options(limit-nvpstate PRIVATE
    # same as the Visual Studio project: the program must run elevated to change P-State limits
    "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
    # plain main() as entry point for the GUI subsystem
    /ENTRY:mainCRTStartup
)

# final package folder: build/limit-nvpstate/
set_target_properties(limit-nvpstate PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/limit-nvpstate
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/limit-nvpstate
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/limit-nvpstate
)

# config.json next to the executable (an existing one is kept so settings survive rebuilds)
add_custom_command(TARGET limit-nvpstate POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DSRC=${SRC_DIR}/config.json
        -DDST=$<TARGET_FILE_DIR:limit-nvpstate>/config.json
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_config.cmake
    VERBATIM
)

# copy the Qt runtime next to the executable
get_target_property(_qmake_executable Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

if(WINDEPLOYQT_EXECUTABLE)
    if(QT_VERSION_MAJOR EQUAL 5)
        set(_deploy_args --no-system-d3d-compiler --no-translations --no-opengl-sw --no-angle)
    else()
        set(_deploy_args --no-system-d3d-compiler --no-translations --no-opengl-sw)
    endif()

    add_custom_command(TARGET limit-nvpstate POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" $<TARGET_FILE:limit-nvpstate> ${_deploy_args}
        VERBATIM
    )
else()
    message(WARNING "windeployqt not found, the Qt DLLs have to be copied next to the executable manually")
endif()
