# One executable runs every *.test.cpp through doctest.
file(GLOB PORTCULLIS_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp")

add_executable(portcullis_tests main.cpp ${PORTCULLIS_TEST_SOURCES})
target_link_libraries(portcullis_tests PRIVATE portcullis::portcullis)
target_include_directories(portcullis_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(portcullis_tests PRIVATE -Wall -Wextra -Werror)
# The vendored doctest includes <ciso646>, which recent libstdc++ marks with a
# deprecation #warning; -Werror would promote that third-party #warning to an
# error (seen with clang over libstdc++ 15). Downgrade only #warning directives
# back to warnings — real diagnostics stay errors. The flag is quoted so CMake
# does not read the '#' as a comment; clang and gcc spell the group differently.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(portcullis_tests PRIVATE "-Wno-error=#warnings")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(portcullis_tests PRIVATE "-Wno-error=cpp")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/third_party/doctest.cmake OPTIONAL)
add_test(NAME portcullis_tests COMMAND portcullis_tests)

# Compile-fail tests: snippets that must NOT compile.
add_subdirectory(compile_fail)
