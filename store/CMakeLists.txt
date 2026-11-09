find_package(PkgConfig REQUIRED)
pkg_check_modules(FLATPAK IMPORTED_TARGET GLOBAL flatpak)
pkg_check_modules(APPSTREAM IMPORTED_TARGET GLOBAL appstream)
if(NOT FLATPAK_FOUND OR NOT APPSTREAM_FOUND)
    message(STATUS "flatpak and appstream are not available, leaving out the store core")
    return()
endif()

add_library(microset_store_core STATIC
    src/flatpakbackend.cpp
    src/flatpakbackend.h
    src/storebackend.h
    src/storecatalog.cpp
    src/storecatalog.h
    src/storecatalogmodel.cpp
    src/storecatalogmodel.h
    src/storeentry.h
    src/storeglibthread.cpp
    src/storeglibthread.h
    src/storejob.cpp
    src/storejob.h
    src/storeworker.cpp
    src/storeworker.h
)
target_include_directories(microset_store_core PUBLIC src)
target_link_libraries(microset_store_core PUBLIC Qt6::Core PkgConfig::FLATPAK PkgConfig::APPSTREAM)
target_compile_options(microset_store_core PRIVATE ${FLATPAK_CFLAGS_OTHER} ${APPSTREAM_CFLAGS_OTHER})

if(NOT MICROSET_UI OR NOT MICROSET_LAUNCHER)
    message(STATUS "microset-store needs MICROSET_UI and microset-launcher, leaving it out")
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
    return()
endif()

find_package(Qt6 6.11 REQUIRED COMPONENTS Gui GuiPrivate Qml Quick WaylandClient WaylandClientPrivate)
find_package(LayerShellQt REQUIRED)

add_library(microset_store_app STATIC
    app/src/storeprocess.cpp
    app/src/storeprocess.h
)
target_include_directories(microset_store_app PUBLIC app/src)
target_link_libraries(microset_store_app PUBLIC microset_store_core microset_launcher_core Qt6::Core Qt6::DBus Qt6::Gui
    Qt6::Qml Qt6::Quick)

set_source_files_properties(qml/Store.qml PROPERTIES QT_RESOURCE_ALIAS Store.qml)
set_source_files_properties(qml/StoreList.qml PROPERTIES QT_RESOURCE_ALIAS StoreList.qml)
set_source_files_properties(qml/StoreDetail.qml PROPERTIES QT_RESOURCE_ALIAS StoreDetail.qml)
set_source_files_properties(qml/StoreRouter.qml PROPERTIES QT_RESOURCE_ALIAS StoreRouter.qml)
qt_add_library(microset_store_ui STATIC)
qt_add_qml_module(microset_store_ui
    URI MicroSetStore
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/imports/MicroSetStore
    IMPORT_PATH ${PROJECT_BINARY_DIR}/imports
    QML_FILES qml/Store.qml qml/StoreList.qml qml/StoreDetail.qml qml/StoreRouter.qml
    DEPENDENCIES MicroSet
)
target_link_libraries(microset_store_ui PUBLIC microset_store_app microset_uiplugin Qt6::Gui Qt6::Qml Qt6::Quick)

qt_add_executable(microset-store app/src/main.cpp)
target_link_libraries(microset-store PRIVATE microset_store_uiplugin microset_uiplugin microset_ui microset_store_app
    LayerShellQt::Interface Qt6::Quick Qt6::WaylandClient Qt6::GuiPrivate Qt6::WaylandClientPrivate)

install(TARGETS microset-store DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/microset-store.service DESTINATION ${MICROSET_SYSTEMD_USER_DIR})
install(FILES data/net.seilent.MicroSet.Store.service DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/dbus-1/services)
install(FILES data/net.seilent.MicroSet.Store.desktop DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/applications)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
