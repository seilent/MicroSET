find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst_overlay tst_overlay.cpp)
target_link_libraries(tst_overlay PRIVATE microset_overlay_core microset_harness microset_page_fixtures microset_ui Qt6::Quick
    Qt6::Test)
add_test(NAME overlay COMMAND tst_overlay)
set_tests_properties(overlay PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_overlayqml tst_overlayqml.cpp)
target_link_libraries(tst_overlayqml PRIVATE microset_overlay_core microset_harness microset_page_fixtures
    microset_overlay_uiplugin microset_uiplugin microset_ui Qt6::Quick Qt6::Test PkgConfig::XCB)
add_test(NAME overlay-qml COMMAND tst_overlayqml)
set_tests_properties(overlay-qml PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_framelimit tst_framelimit.cpp)
target_link_libraries(tst_framelimit PRIVATE microset_gamescope Qt6::Test)

find_program(XVFB_RUN xvfb-run)
if(XVFB_RUN)
    add_test(NAME overlay-qml-x
        COMMAND ${XVFB_RUN} -d -s "-screen 0 960x640x24" $<TARGET_FILE:tst_overlayqml> stripSizeHintsFollowTheTurn)
    set_tests_properties(overlay-qml-x PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=xcb;QT_QUICK_BACKEND=software;QT_XCB_GL_INTEGRATION=none")
    add_test(NAME framelimit-x COMMAND ${XVFB_RUN} -d $<TARGET_FILE:tst_framelimit>)
    set_tests_properties(framelimit-x PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=xcb;QT_XCB_GL_INTEGRATION=none")
else()
    message(WARNING "xvfb-run not found, overlay-qml-x and framelimit-x are not registered")
endif()

add_test(NAME overlay-wait-x COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/wait-x.sh ${PROJECT_SOURCE_DIR}/data/libexec/microset-wait-x)

add_executable(microset-overlayshot overlayshot.cpp)
target_link_libraries(microset-overlayshot PRIVATE microset_harness microset_page_fixtures microset_overlay_uiplugin
    microset_uiplugin microset_ui Qt6::Quick)

if(NOT MICROSET_TEST_FONT_DIR)
    return()
endif()

set(game_states game game-missing game-state game-yield game-other-tool game-gone game-native game-page game-lsfg
    game-lsfg-limit game-lsfg-failed game-lsfg-cap game-lsfg-young game-lsfg-off game-lsfg-no-dll game-lsfg-env
    game-lsfg-check game-lsfg-state game-lsfg-page)
set(states_960x640 top audio hint-page hint sheet undervolt-after-sheet ${game_states})
set(states_640x960 top audio portrait hint-page hint hint-portrait sheet sheet-portrait undervolt-after-sheet ${game_states})
foreach(size 960x640 640x960)
    foreach(state IN LISTS states_${size})
        add_test(NAME overlay-shot-${state}-${size}
            COMMAND microset-overlayshot --screenshot ${CMAKE_CURRENT_BINARY_DIR}/shots/${state}-${size}.png
                --size ${size} --state ${state})
        set_tests_properties(overlay-shot-${state}-${size} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MICROSET_FONT_DIR=${MICROSET_TEST_FONT_DIR}")
    endforeach()
    foreach(state hint-page hint)
        add_test(NAME overlay-shot-undervolt-${state}-${size}
            COMMAND microset-overlayshot --screenshot ${CMAKE_CURRENT_BINARY_DIR}/shots/undervolt-${state}-${size}.png
                --size ${size} --state ${state} --hint-kind undervolt)
        set_tests_properties(overlay-shot-undervolt-${state}-${size} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MICROSET_FONT_DIR=${MICROSET_TEST_FONT_DIR}")
    endforeach()
endforeach()
