find_package(Qt6 REQUIRED COMPONENTS Test)
find_program(DBUS_DAEMON dbus-daemon REQUIRED)
find_program(SETPRIV setpriv REQUIRED)

add_library(microset_harness STATIC harness.cpp fakepowerd.cpp fakealsa.cpp fakelogind.cpp)
target_link_libraries(microset_harness PUBLIC microsetd_core Qt6::Test)
target_compile_definitions(microset_harness PUBLIC
    MICROSET_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    MICROSET_DAEMON="$<TARGET_FILE:microsetd>"
    MICROSET_CLI="$<TARGET_FILE:microset-cli>"
    DBUS_DAEMON="${DBUS_DAEMON}"
    SETPRIV="${SETPRIV}"
)
add_dependencies(microset_harness microsetd microset-cli)

foreach(name store schema sessions import bus client alsa ssc rgb)
    add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE microset_harness libmicroset)
    add_test(NAME ${name} COMMAND tst_${name})
endforeach()
