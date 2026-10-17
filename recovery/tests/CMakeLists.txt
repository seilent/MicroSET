find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst_recovery tst_recovery.cpp)
target_link_libraries(tst_recovery PRIVATE microset_recovery_core Qt6::Test)
add_test(NAME recovery COMMAND tst_recovery)

add_executable(tst_hold tst_hold.cpp)
target_link_libraries(tst_hold PRIVATE microset_recovery_core microset_recovery_uiplugin Qt6::Quick Qt6::Test)
add_test(NAME recovery-hold COMMAND tst_hold)
set_tests_properties(recovery-hold PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

find_program(DBUS_DAEMON dbus-daemon REQUIRED)
add_executable(tst_pad tst_pad.cpp)
target_link_libraries(tst_pad PRIVATE microset_recovery_core microset_recovery_uiplugin Qt6::Quick Qt6::Test)
target_compile_definitions(tst_pad PRIVATE DBUS_DAEMON="${DBUS_DAEMON}")
add_test(NAME recovery-pad COMMAND tst_pad)
set_tests_properties(recovery-pad PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(NOT MICROSET_TEST_FONT_DIR)
    return()
endif()

foreach(state browse rollback long-reason empty-journal short-journal dialog toast no-previous long-line requested hold)
    add_test(NAME recovery-shot-${state}
        COMMAND microset-recovery --screenshot ${CMAKE_CURRENT_BINARY_DIR}/shots/${state}.png --state ${state})
    set_tests_properties(recovery-shot-${state} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MICROSET_FONT_DIR=${MICROSET_TEST_FONT_DIR}"
        FIXTURES_SETUP recovery-shots)
endforeach()

find_program(SHA256SUM sha256sum REQUIRED)
add_test(NAME recovery-shot-compare COMMAND ${SHA256SUM} --check --strict ${CMAKE_CURRENT_SOURCE_DIR}/shots.sha256
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shots)
set_tests_properties(recovery-shot-compare PROPERTIES FIXTURES_REQUIRED recovery-shots)
