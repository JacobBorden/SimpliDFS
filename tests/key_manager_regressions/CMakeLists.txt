# The wrappers inject allocation failures and observe zeroing without changing
# production APIs. They require GNU-compatible ELF linker wrapping.
if(UNIX AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  foreach(suite hex rotation combined)
    set(target KeyManagerRegression_${suite})
    add_executable(${target}
      ${suite}_test.cpp
      ${PROJECT_SOURCE_DIR}/src/utilities/key_manager.cpp)
    target_include_directories(${target} PRIVATE
      ${PROJECT_SOURCE_DIR}/include ${SODIUM_INCLUDE_DIRS})
    target_link_libraries(${target} PRIVATE ${SODIUM_LIBRARIES} Threads::Threads
      "-Wl,--wrap=sodium_malloc" "-Wl,--wrap=sodium_free")
    if(NOT suite STREQUAL "rotation")
      target_link_libraries(${target} PRIVATE "-Wl,--wrap=randombytes_buf")
    endif()
    add_test(NAME KeyManagerRegression.${suite}
      COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/run_regressions.py
        --suite ${suite} --executable $<TARGET_FILE:${target}>)
    set_tests_properties(KeyManagerRegression.${suite} PROPERTIES TIMEOUT 120)
  endforeach()
endif()
