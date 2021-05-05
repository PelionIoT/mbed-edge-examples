if (NOT TARGET_GROUP STREQUAL test)
  file (GLOB SOURCES ./*.c ../lib/${EDGE_SOURCES_DIR_NAME}/common/edge_trace.c)

  include_directories (${CMAKE_CURRENT_LIST_DIR}/include)

  add_executable (c-api-stress-tester ${SOURCES})

  add_definitions(-DMBED_CONF_MBED_TRACE_ENABLE=1)

  target_include_directories (c-api-stress-tester PUBLIC ${CMAKE_CURRENT_LIST_DIR})
  target_include_directories (c-api-stress-tester PUBLIC ${ROOT_HOME}/include)

  target_link_libraries (c-api-stress-tester pthread examples-common pt-client)
endif()
