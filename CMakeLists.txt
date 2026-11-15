cmake_minimum_required(VERSION 3.16)
project(uacwin VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Plasma 6 distros use Qt6 + PolkitQt6-1, Plasma 5 distros (e.g. Kubuntu 24.04)
# use Qt5 + PolkitQt5-1. Prefer Qt6, fall back to Qt5.
option(UACWIN_FORCE_QT5 "Build against Qt5 even if Qt6 is available" OFF)

if(NOT UACWIN_FORCE_QT5)
    find_package(Qt6 6.2 QUIET COMPONENTS Widgets)
    find_package(PolkitQt6-1 QUIET)
endif()

if(Qt6_FOUND AND PolkitQt6-1_FOUND)
    set(UACWIN_QT 6)
else()
    find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets)
    find_package(PolkitQt5-1 REQUIRED)
    set(UACWIN_QT 5)
endif()
message(STATUS "UACWin: building against Qt${UACWIN_QT}")

add_executable(uacwin-agent
    src/main.cpp
    src/sound.cpp
    src/agent.cpp
    src/appinfo.cpp
    src/uacprompt.cpp
)

target_link_libraries(uacwin-agent PRIVATE
    Qt${UACWIN_QT}::Widgets
    PolkitQt${UACWIN_QT}-1::Core
    PolkitQt${UACWIN_QT}-1::Agent
)

include(GNUInstallDirs)
install(TARGETS uacwin-agent DESTINATION ${CMAKE_INSTALL_FULL_LIBEXECDIR})

set(UACWIN_EXEC ${CMAKE_INSTALL_FULL_LIBEXECDIR}/uacwin-agent)
configure_file(data/uacwin-agent.service.in uacwin-agent.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/uacwin-agent.service
        DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
