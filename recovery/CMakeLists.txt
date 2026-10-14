find_package(Qt6 6.11 REQUIRED COMPONENTS Gui Qml Quick)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)

set(MICROSET_RECOVERY_FONT_DIR "${CMAKE_INSTALL_FULL_DATADIR}/fonts/microset" CACHE PATH "Selawik font dir")

add_library(microset_recovery_core STATIC src/recovery.cpp src/recovery.h)
target_include_directories(microset_recovery_core PUBLIC src)
target_link_libraries(microset_recovery_core PUBLIC Qt6::Core Qt6::DBus)

set(recovery_qml
    qml/Main.qml
    qml/RecoveryScreen.qml
    qml/Ui.qml
    qml/Motion.qml
    qml/PadRouter.qml
    qml/PressScale.qml
    qml/FocusBar.qml
    qml/FocusList.qml
    qml/TouchScroll.qml
    qml/components/PageSurface.qml
    qml/components/CroppedHeader.qml
    qml/components/Dialog.qml
    qml/components/Toast.qml
)
foreach(file IN LISTS recovery_qml)
    get_filename_component(alias ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${alias})
endforeach()
set_source_files_properties(qml/Ui.qml qml/Motion.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(microset_recovery_ui STATIC)
qt_add_qml_module(microset_recovery_ui
    URI MicroSetRecovery
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/MicroSetRecovery
    QML_FILES ${recovery_qml}
    SOURCES src/env.cpp src/env.h
)
target_include_directories(microset_recovery_ui PRIVATE src)
target_compile_definitions(microset_recovery_ui PRIVATE MICROSET_RECOVERY_FONT_DIR="${MICROSET_RECOVERY_FONT_DIR}")
target_link_libraries(microset_recovery_ui PUBLIC Qt6::Gui Qt6::Qml Qt6::Quick)

qt_add_executable(microset-recovery src/main.cpp src/gamescope.cpp src/gamescope.h)
target_link_libraries(microset-recovery PRIVATE microset_recovery_core microset_recovery_uiplugin Qt6::Gui Qt6::Qml Qt6::Quick
    PkgConfig::XCB)

install(TARGETS microset-recovery DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
