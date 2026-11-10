find_package(Lua 5.3 REQUIRED)

include(FetchContent)
FetchContent_Declare(sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG eba86625b707e3c8c99bbfc4624e51f42dc9e561
    SOURCE_SUBDIR bms-unused
)
FetchContent_MakeAvailable(sol2)

add_library(bms_lua_session STATIC
    "${CMAKE_CURRENT_LIST_DIR}/../src/integration/LuaScriptSession.cpp"
)
target_compile_features(bms_lua_session PUBLIC cxx_std_17)
target_compile_definitions(bms_lua_session PRIVATE SOL_ALL_SAFETIES_ON=1)
target_include_directories(bms_lua_session
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src"
    PRIVATE "${LUA_INCLUDE_DIR}"
)
target_include_directories(bms_lua_session SYSTEM PRIVATE "${sol2_SOURCE_DIR}/include")
target_link_libraries(bms_lua_session PUBLIC ${LUA_LIBRARIES})
