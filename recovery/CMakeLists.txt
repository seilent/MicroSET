find_package(Qt6 6.11 REQUIRED COMPONENTS Gui Qml Quick)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)

add_library(microset_recovery_core STATIC src/recovery.cpp src/recovery.h)
target_include_directories(microset_recovery_core PUBLIC src)
target_link_libraries(microset_recovery_core PUBLIC microset_pad Qt6::Core Qt6::DBus)

set(recovery_qml
    qml/Main.qml
    qml/RecoveryScreen.qml
)
foreach(file IN LISTS recovery_qml)
    get_filename_component(alias ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${alias})
endforeach()

qt_add_library(microset_recovery_ui STATIC)
qt_add_qml_module(microset_recovery_ui
    URI MicroSetRecovery
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/imports/MicroSetRecovery
    IMPORT_PATH ${PROJECT_BINARY_DIR}/imports
    QML_FILES ${recovery_qml}
    DEPENDENCIES MicroSet
)
target_link_libraries(microset_recovery_ui PUBLIC microset_uiplugin Qt6::Gui Qt6::Qml Qt6::Quick)

qt_add_executable(microset-recovery src/main.cpp src/gamescope.cpp src/gamescope.h)
target_link_libraries(microset-recovery PRIVATE microset_recovery_core microset_recovery_uiplugin Qt6::Gui Qt6::Qml Qt6::Quick
    PkgConfig::XCB)

install(TARGETS microset-recovery DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
