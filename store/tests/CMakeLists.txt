find_package(Qt6 REQUIRED COMPONENTS Test)

add_library(microset_store_testfixtures STATIC fakebackend.cpp fakebackend.h)
target_include_directories(microset_store_testfixtures PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(microset_store_testfixtures PUBLIC microset_store_core)

add_executable(tst_storecatalog tst_storecatalog.cpp)
target_link_libraries(tst_storecatalog PRIVATE microset_store_core microset_store_testfixtures Qt6::Test)
target_compile_definitions(tst_storecatalog PRIVATE MICROSET_STORE_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME store-catalog COMMAND tst_storecatalog)

add_executable(tst_storejob tst_storejob.cpp)
target_link_libraries(tst_storejob PRIVATE microset_store_core Qt6::Test)
add_test(NAME store-job COMMAND tst_storejob)

add_executable(tst_storeworker tst_storeworker.cpp)
target_link_libraries(tst_storeworker PRIVATE microset_store_core microset_store_testfixtures Qt6::Test)
add_test(NAME store-worker COMMAND tst_storeworker)

add_executable(tst_storeflatpak tst_storeflatpak.cpp)
target_link_libraries(tst_storeflatpak PRIVATE microset_store_core Qt6::Test PkgConfig::FLATPAK)
add_test(NAME store-flatpak COMMAND tst_storeflatpak)
set_tests_properties(store-flatpak PROPERTIES TIMEOUT 300)

if(NOT TARGET microset_store_app OR NOT TARGET microset_store_uiplugin)
    return()
endif()

add_executable(tst_storelayout tst_storelayout.cpp)
target_link_libraries(tst_storelayout PRIVATE microset_store_app microset_store_uiplugin microset_uiplugin microset_ui
    microset_store_testfixtures Qt6::Quick Qt6::Test)
target_include_directories(tst_storelayout PRIVATE ${PROJECT_SOURCE_DIR}/tests)
add_test(NAME store-layout COMMAND tst_storelayout)
set_tests_properties(store-layout PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(microset-storeshot storeshot.cpp)
target_link_libraries(microset-storeshot PRIVATE microset_store_app microset_store_uiplugin microset_uiplugin microset_ui
    microset_store_testfixtures Qt6::Quick)

if(NOT MICROSET_TEST_FONT_DIR)
    return()
endif()

set(store_shot_states list list-installed list-many list-noicon detail detail-installed detail-installing detail-queued
    detail-long search search-empty offline confirm toast search-petal search-shift)
foreach(size 480x320 320x480)
    foreach(state IN LISTS store_shot_states)
        add_test(NAME store-shot-${state}-${size}
            COMMAND microset-storeshot --screenshot ${CMAKE_CURRENT_BINARY_DIR}/shots/${state}-${size}.png
                --size ${size} --state ${state})
        set_tests_properties(store-shot-${state}-${size} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MICROSET_FONT_DIR=${MICROSET_TEST_FONT_DIR}")
    endforeach()
endforeach()
