cmake_minimum_required(VERSION 3.20)
project(BMUSimulatorTests LANGUAGES CXX)

enable_testing()
find_package(Threads REQUIRED)

add_executable(cyclic_can_sender_test
    cyclic_can_sender_test.cpp
    ../src/can/CyclicCanSender.cpp
)
target_compile_features(cyclic_can_sender_test PRIVATE cxx_std_17)
target_include_directories(cyclic_can_sender_test PRIVATE ../src)
target_link_libraries(cyclic_can_sender_test PRIVATE Threads::Threads)
add_test(NAME cyclic_can_sender_test COMMAND cyclic_can_sender_test)

option(BMS_TEST_LUA "Build the Lua session test" OFF)
if(BMS_TEST_LUA)
    include(../cmake/LuaSession.cmake)
    add_executable(lua_script_session_test lua_script_session_test.cpp)
    target_compile_features(lua_script_session_test PRIVATE cxx_std_17)
    target_link_libraries(lua_script_session_test PRIVATE bms_lua_session)
    add_test(
        NAME lua_script_session_test
        COMMAND lua_script_session_test
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/session.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/second_session.lua"
    )
endif()
