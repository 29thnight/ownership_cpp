cmake_minimum_required(VERSION 3.21)
project(ownership_cpp LANGUAGES CXX)

# Header-only. Consumers link own::ownership for the include path and C++20.
add_library(ownership_cpp INTERFACE)
add_library(own::ownership ALIAS ownership_cpp)
target_include_directories(ownership_cpp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(ownership_cpp INTERFACE cxx_std_20)

include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS ownership_cpp EXPORT ownership_cpp_targets)
install(EXPORT ownership_cpp_targets NAMESPACE own:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ownership_cpp
    FILE ownership_cppConfig.cmake)

option(OWN_BUILD_TESTS "Build and register the ownership_cpp tests" ${PROJECT_IS_TOP_LEVEL})
if(OWN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
