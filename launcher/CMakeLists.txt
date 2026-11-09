enable_language(C)

find_package(Qt6 6.11 REQUIRED COMPONENTS Gui GuiPrivate Qml Quick WaylandClient WaylandClientPrivate)
find_package(LayerShellQt REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-unix-2.0)

find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
set(MICROSET_IDLE_XML "${WAYLAND_PROTOCOLS_DIR}/unstable/idle-inhibit/idle-inhibit-unstable-v1.xml")
if(NOT EXISTS "${MICROSET_IDLE_XML}")
    message(FATAL_ERROR "idle-inhibit-unstable-v1.xml is missing, install wayland-protocols-devel")
endif()

set(MICROSET_IDLE_HEADER "${CMAKE_CURRENT_BINARY_DIR}/idle-inhibit-unstable-v1-client-protocol.h")
set(MICROSET_IDLE_CODE "${CMAKE_CURRENT_BINARY_DIR}/idle-inhibit-unstable-v1-protocol.c")
add_custom_command(
    OUTPUT "${MICROSET_IDLE_HEADER}"
    COMMAND ${WAYLAND_SCANNER} client-header "${MICROSET_IDLE_XML}" "${MICROSET_IDLE_HEADER}"
    DEPENDS "${MICROSET_IDLE_XML}"
    VERBATIM
)
add_custom_command(
    OUTPUT "${MICROSET_IDLE_CODE}"
    COMMAND ${WAYLAND_SCANNER} private-code "${MICROSET_IDLE_XML}" "${MICROSET_IDLE_CODE}"
    DEPENDS "${MICROSET_IDLE_XML}"
    VERBATIM
)

add_library(microset_launcher_core STATIC
    src/activator.cpp
    src/activator.h
    src/appmodel.cpp
    src/appmodel.h
    src/battery.cpp
    src/battery.h
    src/clock.cpp
    src/clock.h
    src/glibthread.cpp
    src/glibthread.h
    src/desktophome.cpp
    src/desktophome.h
    src/homeclient.h
    src/idlepulse.cpp
    src/idlepulse.h
    src/launch.cpp
    src/launch.h
    src/launcher.cpp
    src/launcher.h
    src/poweraction.cpp
    src/poweraction.h
    src/waylandidle.cpp
    src/waylandidle.h
    src/wifi.cpp
    src/wifi.h
    "${MICROSET_IDLE_CODE}"
    "${MICROSET_IDLE_HEADER}"
)
target_include_directories(microset_launcher_core PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}" ${PROJECT_SOURCE_DIR}/qml/src)
set_source_files_properties("${MICROSET_IDLE_CODE}" PROPERTIES GENERATED TRUE)
target_link_libraries(microset_launcher_core PUBLIC microset_pad microset_common Qt6::Core Qt6::DBus Qt6::Gui
    Qt6::Qml Qt6::Quick Qt6::WaylandClient PkgConfig::GIO wayland-client)
target_compile_options(microset_launcher_core PRIVATE ${GIO_CFLAGS_OTHER})


set_source_files_properties(qml/Home.qml PROPERTIES QT_RESOURCE_ALIAS Home.qml)
set_source_files_properties(qml/PowerPage.qml PROPERTIES QT_RESOURCE_ALIAS PowerPage.qml)
set_source_files_properties(qml/LauncherRouter.qml PROPERTIES QT_RESOURCE_ALIAS LauncherRouter.qml)
set_source_files_properties(qml/HomeStack.qml PROPERTIES QT_RESOURCE_ALIAS HomeStack.qml)
qt_add_library(microset_launcher_ui STATIC)
qt_add_qml_module(microset_launcher_ui
    URI MicroSetLauncher
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/imports/MicroSetLauncher
    IMPORT_PATH ${PROJECT_BINARY_DIR}/imports
    QML_FILES qml/Home.qml qml/PowerPage.qml qml/LauncherRouter.qml qml/HomeStack.qml
    DEPENDENCIES MicroSet
)
target_link_libraries(microset_launcher_ui PUBLIC microset_launcher_core microset_uiplugin Qt6::Gui Qt6::Qml Qt6::Quick)

qt_add_executable(microset-launcher src/main.cpp)
target_link_libraries(microset-launcher PRIVATE microset_launcher_core microset_launcher_uiplugin microset_uiplugin
    microset_ui LayerShellQt::Interface Qt6::Quick Qt6::WaylandClient Qt6::GuiPrivate Qt6::WaylandClientPrivate)

install(TARGETS microset-launcher DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/microset-launcher.service DESTINATION ${MICROSET_SYSTEMD_USER_DIR})
install(FILES data/microos-desktop-ready.target.d/30-microset-launcher.conf
    DESTINATION ${MICROSET_SYSTEMD_USER_DIR}/microos-desktop-ready.target.d)
install(FILES data/net.seilent.MicroSet.Launcher.service
    DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/dbus-1/services)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
