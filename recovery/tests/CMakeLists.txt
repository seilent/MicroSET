find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst_recovery tst_recovery.cpp)
target_link_libraries(tst_recovery PRIVATE microset_recovery_core Qt6::Test)
add_test(NAME recovery COMMAND tst_recovery)

add_executable(tst_hold tst_hold.cpp)
target_link_libraries(tst_hold PRIVATE microset_recovery_core microset_recovery_uiplugin Qt6::Quick Qt6::Test)
add_test(NAME recovery-hold COMMAND tst_hold)
set_tests_properties(recovery-hold PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

set(MICROSET_TEST_FONT_DIR "" CACHE PATH "Selawik dir for the screenshot tests, empty skips them")
if(NOT MICROSET_TEST_FONT_DIR)
    return()
endif()

foreach(state browse rollback long-reason empty-journal short-journal dialog toast no-previous long-line requested hold)
    add_test(NAME recovery-shot-${state}
        COMMAND microset-recovery --screenshot ${CMAKE_CURRENT_BINARY_DIR}/shots/${state}.png --state ${state})
    set_tests_properties(recovery-shot-${state} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MICROSET_FONT_DIR=${MICROSET_TEST_FONT_DIR}")
endforeach()
