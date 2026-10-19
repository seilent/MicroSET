cmake_minimum_required(VERSION 3.22)
project(MicroSET VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core DBus)
find_package(ALSA REQUIRED)
qt_standard_project_setup()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

set(MICROSET_SYSTEMD_UNIT_DIR "${CMAKE_INSTALL_PREFIX}/lib/systemd/system" CACHE PATH "systemd system unit dir")
set(MICROSET_SYSUSERS_DIR "${CMAKE_INSTALL_PREFIX}/lib/sysusers.d" CACHE PATH "sysusers.d dir")
set(MICROSET_DBUS_POLICY_DIR "${CMAKE_INSTALL_FULL_DATADIR}/dbus-1/system.d" CACHE PATH "D-Bus system policy dir")
set(MICROSET_DBUS_INTERFACES_DIR "${CMAKE_INSTALL_FULL_DATADIR}/dbus-1/interfaces" CACHE PATH "D-Bus interface XML dir")
set(MICROSET_SCHEMA_DIR "${CMAKE_INSTALL_FULL_DATADIR}/microset/schema.d" CACHE PATH "schema.d dir")
set(MICROSET_SYSTEMD_USER_DIR "${CMAKE_INSTALL_PREFIX}/lib/systemd/user" CACHE PATH "systemd user unit dir")
set(MICROSET_HOTKEYS_DIR "${CMAKE_INSTALL_PREFIX}/lib/microos/hotkeys.d" CACHE PATH "MicroOS hotkeyd table dir")

add_library(microset_common STATIC daemon/auth.cpp daemon/auth.h daemon/sleepinhibitor.cpp daemon/sleepinhibitor.h)
target_include_directories(microset_common PUBLIC daemon)
target_link_libraries(microset_common PUBLIC Qt6::Core Qt6::DBus)

add_library(microsetd_core STATIC
    daemon/alsaapplier.cpp
    daemon/alsalibcontrol.cpp
    daemon/backlightapplier.cpp
    daemon/powerbridge.cpp
    daemon/qrtrlink.cpp
    daemon/rgbapplier.cpp
    daemon/schema.cpp
    daemon/service.cpp
    daemon/sessionsprovider.cpp
    daemon/settings.cpp
    daemon/sscapplier.cpp
    daemon/ssclink.h
    daemon/sscrotation.cpp
    daemon/sscwire.cpp
    daemon/store.cpp
)
target_include_directories(microsetd_core PUBLIC daemon lib)
target_link_libraries(microsetd_core PUBLIC microset_common Qt6::Core Qt6::DBus ALSA::ALSA)

add_executable(microsetd daemon/main.cpp)
target_link_libraries(microsetd PRIVATE microsetd_core)
target_compile_definitions(microsetd PRIVATE MICROSET_SCHEMA_DIR="${MICROSET_SCHEMA_DIR}")

add_library(libmicroset SHARED lib/client.cpp lib/microset/client.h lib/microset/color.h)
add_library(MicroSet::MicroSet ALIAS libmicroset)
set_target_properties(libmicroset PROPERTIES
    OUTPUT_NAME microset
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    EXPORT_NAME MicroSet
)
target_include_directories(libmicroset PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(libmicroset PUBLIC Qt6::Core Qt6::DBus)

add_executable(microset-cli cli/main.cpp)
set_target_properties(microset-cli PROPERTIES OUTPUT_NAME microset)
target_link_libraries(microset-cli PRIVATE libmicroset)

set(MICROSET_LIBEXEC "${CMAKE_INSTALL_FULL_LIBEXECDIR}")
configure_file(data/microsetd.service.in microsetd.service @ONLY)

install(TARGETS microsetd DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(TARGETS microset-cli DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS libmicroset EXPORT MicroSetTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES lib/microset/client.h lib/microset/color.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/microset)
install(EXPORT MicroSetTargets NAMESPACE MicroSet:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MicroSet)
configure_package_config_file(cmake/MicroSetConfig.cmake.in MicroSetConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MicroSet
)
write_basic_package_version_file(MicroSetConfigVersion.cmake COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/MicroSetConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/MicroSetConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MicroSet
)

install(FILES schema/10-microset.json DESTINATION ${MICROSET_SCHEMA_DIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/microsetd.service DESTINATION ${MICROSET_SYSTEMD_UNIT_DIR})
install(FILES data/net.seilent.MicroSet.conf DESTINATION ${MICROSET_DBUS_POLICY_DIR})
install(FILES data/net.seilent.MicroSet1.xml DESTINATION ${MICROSET_DBUS_INTERFACES_DIR})
install(FILES data/microset.conf DESTINATION ${MICROSET_SYSUSERS_DIR})

option(MICROSET_UI "Build the MicroSet QML module and the pad library" ON)
set(MICROSET_TEST_FONT_DIR "" CACHE PATH "Selawik dir for the screenshot tests, empty skips them")
option(MICROSET_RECOVERY "Build microset-recovery" ON)
option(MICROSET_SETTINGS "Build microset-settings" ${MICROSET_UI})
if((MICROSET_RECOVERY OR MICROSET_SETTINGS) AND NOT MICROSET_UI)
    message(FATAL_ERROR "MICROSET_RECOVERY and MICROSET_SETTINGS need MICROSET_UI")
endif()
if(MICROSET_UI)
    add_library(microset_pad STATIC pad/dbuspad.cpp pad/dbuspad.h)
    target_include_directories(microset_pad PUBLIC pad)
    target_link_libraries(microset_pad PUBLIC Qt6::Core Qt6::DBus)
    add_subdirectory(qml)
endif()
if(MICROSET_RECOVERY OR MICROSET_SETTINGS)
    find_package(Qt6 6.11 REQUIRED COMPONENTS Gui)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)
    add_library(microset_gamescope STATIC gamescope/gamescope.cpp gamescope/gamescope.h)
    target_include_directories(microset_gamescope PUBLIC gamescope)
    target_link_libraries(microset_gamescope PUBLIC Qt6::Gui PkgConfig::XCB)
endif()
if(MICROSET_RECOVERY)
    add_subdirectory(recovery)
endif()
if(MICROSET_SETTINGS)
    add_subdirectory(app)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
