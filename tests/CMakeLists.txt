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
