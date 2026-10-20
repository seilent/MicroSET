find_package(Qt6 REQUIRED COMPONENTS Test)

add_library(microset_page_fixtures STATIC fakemicrosetd.cpp fakemicrosetd.h)
target_include_directories(microset_page_fixtures PUBLIC .)
target_link_libraries(microset_page_fixtures PUBLIC microsetd_core Qt6::DBus)
target_compile_definitions(microset_page_fixtures PUBLIC
    MICROSET_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    MICROSET_PAGE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

qt_add_library(microset_pagetest STATIC)
qt_add_qml_module(microset_pagetest
    URI MicroSetPageTest
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/imports/MicroSetPageTest
    IMPORT_PATH ${PROJECT_BINARY_DIR}/imports
    QML_FILES PageRoot.qml
    DEPENDENCIES MicroSet
)
target_link_libraries(microset_pagetest PUBLIC microset_uiplugin Qt6::Quick)

add_executable(microset-pageshot pageshot.cpp)
target_link_libraries(microset-pageshot PRIVATE microset_page_fixtures microset_pagetestplugin microset_uiplugin microset_ui
    Qt6::Quick)

add_executable(tst_model tst_model.cpp)
target_link_libraries(tst_model PRIVATE microset_harness microset_page_fixtures microset_ui Qt6::Test)
add_test(NAME page-model COMMAND tst_model)

add_executable(tst_page tst_page.cpp)
target_link_libraries(tst_page PRIVATE microset_harness microset_page_fixtures microset_pagetestplugin microset_uiplugin microset_ui
    Qt6::Quick Qt6::Test)
add_test(NAME page COMMAND tst_page)
set_tests_properties(page PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(NOT MICROSET_TEST_FONT_DIR)
    return()
endif()

foreach(size 960x640 640x960)
    foreach(state top audio display rgb rgb-off power system updates edit hue-edit draft-edit toast long-label long-value merged top-sections
            undervolt-off undervolt undervolt-next undervolt-waiting undervolt-fallback undervolt-long undervolt-changed
            undervolt-reason-focus undervolt-hold undervolt-refused undervolt-busy)
        add_test(NAME page-shot-${state}-${size}
            COMMAND microset-pageshot --screenshot ${CMAKE_CURRENT_BINARY_DIR}/shots/${state}-${size}.png
                --size ${size} --state ${state})
        set_tests_properties(page-shot-${state}-${size} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MICROSET_FONT_DIR=${MICROSET_TEST_FONT_DIR}")
    endforeach()
endforeach()
