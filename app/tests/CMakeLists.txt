find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst_overlay tst_overlay.cpp)
target_link_libraries(tst_overlay PRIVATE microset_overlay_core microset_harness Qt6::Quick Qt6::Test)
add_test(NAME overlay COMMAND tst_overlay)
set_tests_properties(overlay PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_overlayqml tst_overlayqml.cpp)
target_link_libraries(tst_overlayqml PRIVATE microset_overlay_core microset_page_fixtures microset_overlay_uiplugin
    microset_uiplugin microset_ui Qt6::Quick Qt6::Test)
add_test(NAME overlay-qml COMMAND tst_overlayqml)
set_tests_properties(overlay-qml PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(NAME overlay-wait-x COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/wait-x.sh ${PROJECT_SOURCE_DIR}/data/libexec/microset-wait-x)

add_executable(microset-overlayshot overlayshot.cpp)
target_link_libraries(microset-overlayshot PRIVATE microset_page_fixtures microset_overlay_uiplugin microset_uiplugin
    microset_ui Qt6::Quick)

if(NOT MICROSET_TEST_FONT_DIR)
    return()
endif()

set(states_960x640 top audio hint-page hint)
set(states_640x960 top audio portrait hint-page hint hint-portrait)
foreach(size 960x640 640x960)
    foreach(state IN LISTS states_${size})
        add_test(NAME overlay-shot-${state}-${size}
            COMMAND microset-overlayshot --screenshot ${CMAKE_CURRENT_BINARY_DIR}/shots/${state}-${size}.png
                --size ${size} --state ${state})
        set_tests_properties(overlay-shot-${state}-${size} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MICROSET_FONT_DIR=${MICROSET_TEST_FONT_DIR}")
    endforeach()
endforeach()
