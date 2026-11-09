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
