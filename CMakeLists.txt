cmake_minimum_required(VERSION 3.16)
project(fcitx5-boshiamy VERSION 1.0.0)

set(REQUIRED_FCITX_VERSION 5.1.13)
find_package(ECM REQUIRED 1.0.0)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})

find_package(Fcitx5Core ${REQUIRED_FCITX_VERSION} REQUIRED)
find_package(Fcitx5Config ${REQUIRED_FCITX_VERSION} REQUIRED)
find_package(Fcitx5Utils ${REQUIRED_FCITX_VERSION} REQUIRED)
find_package(Fcitx5Module REQUIRED COMPONENTS TestFrontend)

include(FeatureSummary)
include(GNUInstallDirs)
include(ECMUninstallTarget)
include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")

option(ENABLE_TEST "Build Test" ON)

add_subdirectory(src)
add_subdirectory(data)
add_subdirectory(icons)

if (ENABLE_TEST)
    enable_testing()
    add_subdirectory(test)
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
