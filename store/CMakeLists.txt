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

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
