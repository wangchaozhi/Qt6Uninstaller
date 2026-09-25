cmake_minimum_required(VERSION 3.21)

project(Qt6Uninstaller VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)
qt_standard_project_setup()

qt_add_executable(Qt6Uninstaller WIN32
    src/main.cpp
    src/uninstallconfig.cpp
    src/uninstallconfig.h
    src/uninstallerwindow.cpp
    src/uninstallerwindow.h
)

target_link_libraries(Qt6Uninstaller PRIVATE Qt6::Core Qt6::Widgets)
target_compile_definitions(Qt6Uninstaller PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
)

if(WIN32)
    add_executable(cleanup_helper WIN32 src/cleanup_helper.cpp)
    target_compile_definitions(cleanup_helper PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/uninstall-config.json"
    "${CMAKE_CURRENT_BINARY_DIR}/uninstall-config.json"
    COPYONLY
)

install(TARGETS Qt6Uninstaller cleanup_helper RUNTIME DESTINATION bin)
install(FILES uninstall-config.json DESTINATION bin)
install(FILES uninstaller.marker DESTINATION .)

qt_generate_deploy_app_script(
    TARGET Qt6Uninstaller
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT "${deploy_script}")
