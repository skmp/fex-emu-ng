cmake_minimum_required(VERSION 3.14)
project(FEXLinuxTests)

set(CMAKE_CXX_STANDARD 17)

set (X86_C_COMPILER "x86_64-linux-gnu-gcc" CACHE STRING "c compiler for compiling x86 guest libs")
set (X86_CXX_COMPILER "x86_64-linux-gnu-g++" CACHE STRING "c++ compiler for compiling x86 guest libs")

set(CMAKE_C_COMPILER "${X86_C_COMPILER}")
set(CMAKE_CXX_COMPILER "${X86_CXX_COMPILER}")

unset (CMAKE_C_FLAGS)
unset (CMAKE_CXX_FLAGS)

set(GENERATE_GUEST_INSTALL_TARGETS TRUE)

file(GLOB_RECURSE TESTS CONFIGURE_DEPENDS *.cpp)

foreach(TEST ${TESTS})
  get_filename_component(TEST_NAME ${TEST} NAME_WLE)

  file(READ ${TEST} TEST_CODE)
  set(FLAGS_REGEX "//flags: ([^\n]+)")

  string(REGEX MATCH ${FLAGS_REGEX} BUILD_FLAGS ${TEST_CODE})
  # if cannot handle multiline variables, so we have to match the line first
  if(${BUILD_FLAGS} MATCHES ${FLAGS_REGEX})
    set(BUILD_FLAGS "${CMAKE_MATCH_1}")
  else()
    set(BUILD_FLAGS "-g -O2")
  endif()

  set(FLAGS_REGEX "//append flags: ([^\n]+)")

  string(REGEX MATCH ${FLAGS_REGEX} APPEND_FLAGS ${TEST_CODE})
  # if cannot handle multiline variables, so we have to match the line first
  if(${APPEND_FLAGS} MATCHES ${FLAGS_REGEX})
    set(BUILD_FLAGS "${BUILD_FLAGS} ${CMAKE_MATCH_1}" )
  endif()

  set(FLAGS_REGEX "//libs: ([^\n]+)")

  string(REGEX MATCH ${FLAGS_REGEX} LIBS ${TEST_CODE})
  # if cannot handle multiline variables, so we have to match the line first
  if(${LIBS} MATCHES ${FLAGS_REGEX})
    set(LIBS "${CMAKE_MATCH_1}" )
    string(REGEX REPLACE " |," ";" LIBS "${LIBS}")
  else()
    set(LIBS "")
  endif()

  set(BIN_NAME_32 "${TEST_NAME}.32")
  set(BIN_NAME_64 "${TEST_NAME}.64")

  add_executable(${BIN_NAME_32} ${TEST})
  set_target_properties(${BIN_NAME_32} PROPERTIES COMPILE_FLAGS "-m32 ${BUILD_FLAGS}" LINK_FLAGS "-m32")
  target_link_libraries(${BIN_NAME_32} ${LIBS})

  add_executable(${BIN_NAME_64} ${TEST})
  set_target_properties(${BIN_NAME_64} PROPERTIES COMPILE_FLAGS "${BUILD_FLAGS}" LINK_FLAGS "")
  target_link_libraries(${BIN_NAME_64} ${LIBS})

endforeach()
