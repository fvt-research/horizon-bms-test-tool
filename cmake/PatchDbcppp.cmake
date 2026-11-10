# Keep the pinned dependency reproducible without requiring a private fork.
find_package(Git REQUIRED)
set(_dbcppp_patch "${CMAKE_CURRENT_LIST_DIR}/../patches/dbcppp-mingw.patch")
set(_dbcppp_source "${CMAKE_CURRENT_LIST_DIR}/../third_party/dbcppp")
if(NOT EXISTS "${_dbcppp_source}/CMakeLists.txt")
    message(FATAL_ERROR "Missing dbcppp submodule. Run: git submodule update --init --recursive")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${_dbcppp_patch}"
    WORKING_DIRECTORY "${_dbcppp_source}"
    RESULT_VARIABLE _dbcppp_already_patched
    OUTPUT_QUIET ERROR_QUIET)
if(NOT _dbcppp_already_patched EQUAL 0)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --check "${_dbcppp_patch}"
        WORKING_DIRECTORY "${_dbcppp_source}"
        RESULT_VARIABLE _dbcppp_check
        ERROR_VARIABLE _dbcppp_error)
    if(NOT _dbcppp_check EQUAL 0)
        message(FATAL_ERROR "Cannot apply dbcppp portability patch: ${_dbcppp_error}")
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply "${_dbcppp_patch}"
        WORKING_DIRECTORY "${_dbcppp_source}"
        RESULT_VARIABLE _dbcppp_apply
        ERROR_VARIABLE _dbcppp_error)
    if(NOT _dbcppp_apply EQUAL 0)
        message(FATAL_ERROR "Failed to apply dbcppp portability patch: ${_dbcppp_error}")
    endif()
endif()
