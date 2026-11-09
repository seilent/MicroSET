find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst_launcher tst_launcher.cpp)
target_link_libraries(tst_launcher PRIVATE microset_launcher_core microset_harness Qt6::Test)
add_test(NAME launcher COMMAND tst_launcher)
set_tests_properties(launcher PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_launcherapps tst_launcherapps.cpp)
target_link_libraries(tst_launcherapps PRIVATE microset_launcher_core microset_harness Qt6::Test)
add_test(NAME launcher-apps COMMAND tst_launcherapps)
set_tests_properties(launcher-apps PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_launcherstatus tst_launcherstatus.cpp)
target_link_libraries(tst_launcherstatus PRIVATE microset_launcher_core microset_harness Qt6::Test)
add_test(NAME launcher-status COMMAND tst_launcherstatus)
set_tests_properties(launcher-status PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_launcherlayout tst_launcherlayout.cpp)
target_link_libraries(tst_launcherlayout PRIVATE microset_launcher_core microset_launcher_uiplugin microset_uiplugin
    microset_ui microset_harness Qt6::Quick Qt6::Test)
target_include_directories(tst_launcherlayout PRIVATE ${PROJECT_SOURCE_DIR}/tests)
add_test(NAME launcher-layout COMMAND tst_launcherlayout)
set_tests_properties(launcher-layout PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_launcherfavorites tst_launcherfavorites.cpp)
target_link_libraries(tst_launcherfavorites PRIVATE microset_launcher_core microset_page_fixtures Qt6::Test)
add_test(NAME launcher-favorites COMMAND tst_launcherfavorites)
set_tests_properties(launcher-favorites PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
