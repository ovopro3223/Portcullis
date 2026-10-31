# add_compile_fail_test(<name>): compiling <name>.cpp must fail. A paired
# control file that MUST compile guards against a broken include path making
# every negative test pass for the wrong reason.
function(add_compile_fail_test name)
  add_test(NAME "cf.${name}"
    COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -fsyntax-only
            -I${PROJECT_SOURCE_DIR}/include
            -I${PROJECT_SOURCE_DIR}/tests
            ${CMAKE_CURRENT_SOURCE_DIR}/${name}.cpp)
  set_tests_properties("cf.${name}" PROPERTIES WILL_FAIL TRUE)
endfunction()

function(add_compile_pass_test name)
  add_test(NAME "cf.${name}"
    COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -fsyntax-only
            -I${PROJECT_SOURCE_DIR}/include
            -I${PROJECT_SOURCE_DIR}/tests
            ${CMAKE_CURRENT_SOURCE_DIR}/${name}.cpp)
endfunction()

add_compile_pass_test(control)

add_compile_fail_test(untrusted_no_raw_access)
add_compile_fail_test(forward_ref)
add_compile_fail_test(ref_to_bytes)
