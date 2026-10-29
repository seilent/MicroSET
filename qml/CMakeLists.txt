find_package(Qt6 6.11 REQUIRED COMPONENTS Gui Qml Quick)

set(MICROSET_UI_FONT_DIR "${CMAKE_INSTALL_FULL_DATADIR}/fonts/microset" CACHE PATH "Selawik font dir")

set(microset_qml
    MicroSet/Ui.qml
    MicroSet/Motion.qml
    MicroSet/PadRouter.qml
    MicroSet/PressScale.qml
    MicroSet/FocusBar.qml
    MicroSet/FocusList.qml
    MicroSet/TouchScroll.qml
    MicroSet/Turnstile.qml
    MicroSet/PageStack.qml
    MicroSet/Page.qml
    MicroSet/components/PageSurface.qml
    MicroSet/components/CroppedHeader.qml
    MicroSet/components/Dialog.qml
    MicroSet/components/Toast.qml
    MicroSet/components/Sheet.qml
    MicroSet/SettingsRow.qml
    MicroSet/SettingsPage.qml
    MicroSet/GroupsPage.qml
    MicroSet/SettingsView.qml
)
foreach(file IN LISTS microset_qml)
    get_filename_component(alias ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${alias})
endforeach()
set_source_files_properties(MicroSet/Ui.qml MicroSet/Motion.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(microset_ui STATIC)
qt_add_qml_module(microset_ui
    URI MicroSet
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/imports/MicroSet
    QML_FILES ${microset_qml}
    SOURCES src/env.cpp src/env.h src/settingsmodel.cpp src/settingsmodel.h src/steamlibrary.cpp src/steamlibrary.h
        src/updateinfo.cpp src/updateinfo.h src/lsfgstate.cpp src/lsfgstate.h
)
target_include_directories(microset_ui PUBLIC src)
target_compile_definitions(microset_ui PRIVATE MICROSET_UI_FONT_DIR="${MICROSET_UI_FONT_DIR}")
target_link_libraries(microset_ui PUBLIC libmicroset Qt6::DBus Qt6::Gui Qt6::Qml Qt6::Quick)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
