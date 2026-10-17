find_package(Qt6 6.11 REQUIRED COMPONENTS Gui Qml Quick)

add_library(microset_overlay_core STATIC
    src/holdhint.cpp
    src/holdhint.h
    src/overlay.cpp
    src/overlay.h
    src/overlayservice.cpp
    src/overlayservice.h
)
target_include_directories(microset_overlay_core PUBLIC src)
target_link_libraries(microset_overlay_core PUBLIC microset_pad microset_common Qt6::Gui Qt6::DBus)

set_source_files_properties(qml/Overlay.qml PROPERTIES QT_RESOURCE_ALIAS Overlay.qml)
qt_add_library(microset_overlay_ui STATIC)
qt_add_qml_module(microset_overlay_ui
    URI MicroSetOverlay
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/imports/MicroSetOverlay
    IMPORT_PATH ${PROJECT_BINARY_DIR}/imports
    QML_FILES qml/Overlay.qml
    DEPENDENCIES MicroSet
)
target_link_libraries(microset_overlay_ui PUBLIC microset_uiplugin Qt6::Gui Qt6::Qml Qt6::Quick)

qt_add_executable(microset-settings src/main.cpp)
target_link_libraries(microset-settings PRIVATE microset_overlay_core microset_overlay_uiplugin microset_uiplugin
    microset_ui microset_gamescope Qt6::Quick)

install(TARGETS microset-settings DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS ${PROJECT_SOURCE_DIR}/data/libexec/microset-wait-x DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/microset)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/data/systemd/user/ DESTINATION ${MICROSET_SYSTEMD_USER_DIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/data/microos/hotkeys.d/ DESTINATION ${MICROSET_HOTKEYS_DIR})

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
