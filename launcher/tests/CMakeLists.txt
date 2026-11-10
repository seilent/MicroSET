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

if(TARGET microset_store_testfixtures)
    target_link_libraries(tst_launcher PRIVATE microset_store_testfixtures)
    target_link_libraries(tst_launcherlayout PRIVATE microset_store_testfixtures)
endif()

add_executable(tst_launcherfavorites tst_launcherfavorites.cpp)
target_link_libraries(tst_launcherfavorites PRIVATE microset_launcher_core microset_page_fixtures Qt6::Test)
add_test(NAME launcher-favorites COMMAND tst_launcherfavorites)
set_tests_properties(launcher-favorites PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(microset-launchershot launchershot.cpp)
target_link_libraries(microset-launchershot PRIVATE microset_launcher_core microset_launcher_uiplugin microset_uiplugin
    microset_ui Qt6::Quick)

if(NOT MICROSET_TEST_FONT_DIR)
    return()
endif()

set(shot_states home home-empty home-running home-long allapps allapps-zero allapps-many allapps-long allapps-deep
    sheet sheet-allapps jump power-home power-allapps toast confirm home-battery-low)
foreach(size 480x320 320x480)
    foreach(state IN LISTS shot_states)
        add_test(NAME launcher-shot-${state}-${size}
            COMMAND microset-launchershot --screenshot ${CMAKE_CURRENT_BINARY_DIR}/shots/${state}-${size}.png
                --size ${size} --state ${state})
        set_tests_properties(launcher-shot-${state}-${size} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MICROSET_FONT_DIR=${MICROSET_TEST_FONT_DIR}")
    endforeach()
endforeach()
