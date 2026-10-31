# One executable runs every *.test.cpp through doctest.
file(GLOB PORTCULLIS_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp")

add_executable(portcullis_tests main.cpp ${PORTCULLIS_TEST_SOURCES})
target_link_libraries(portcullis_tests PRIVATE portcullis::portcullis)
target_include_directories(portcullis_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(portcullis_tests PRIVATE -Wall -Wextra -Werror)

include(${CMAKE_CURRENT_SOURCE_DIR}/third_party/doctest.cmake OPTIONAL)
add_test(NAME portcullis_tests COMMAND portcullis_tests)

# Compile-fail tests: snippets that must NOT compile.
add_subdirectory(compile_fail)
