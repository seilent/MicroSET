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

add_library(microsetd_core STATIC
    daemon/alsaapplier.cpp
    daemon/alsalibcontrol.cpp
    daemon/auth.cpp
    daemon/powerbridge.cpp
    daemon/qrtrlink.cpp
    daemon/rgbapplier.cpp
    daemon/schema.cpp
    daemon/service.cpp
    daemon/sessionsprovider.cpp
    daemon/settings.cpp
    daemon/sleepinhibitor.cpp
    daemon/sscapplier.cpp
    daemon/ssclink.h
    daemon/sscrotation.cpp
    daemon/sscwire.cpp
    daemon/store.cpp
)
target_include_directories(microsetd_core PUBLIC daemon)
target_link_libraries(microsetd_core PUBLIC Qt6::Core Qt6::DBus ALSA::ALSA)

add_executable(microsetd daemon/main.cpp)
target_link_libraries(microsetd PRIVATE microsetd_core)
target_compile_definitions(microsetd PRIVATE MICROSET_SCHEMA_DIR="${MICROSET_SCHEMA_DIR}")

add_library(libmicroset SHARED lib/client.cpp lib/microset/client.h)
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
install(FILES lib/microset/client.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/microset)
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

option(MICROSET_RECOVERY "Build microset-recovery" ON)
if(MICROSET_RECOVERY)
    add_subdirectory(recovery)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
