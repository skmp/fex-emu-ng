add_executable(thunkgentest generator.cpp)
target_link_libraries(thunkgentest PRIVATE Catch2::Catch2WithMain)
target_link_libraries(thunkgentest PRIVATE thunkgenlib)
catch_discover_tests(thunkgentest TEST_SUFFIX ".ThunkGen")

execute_process(COMMAND "nproc" OUTPUT_VARIABLE CORES)
string(STRIP ${CORES} CORES)

add_custom_target(
  thunkgen_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  USES_TERMINAL
  COMMAND "ctest" "--timeout" "302" "-j${CORES}" "-R" "\.*.ThunkGen")
add_dependencies(thunkgen_tests thunkgentest)
