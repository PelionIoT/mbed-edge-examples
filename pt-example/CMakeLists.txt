file (GLOB SOURCES ./*.c ../lib/${EDGE_SOURCES_DIR_NAME}/common/edge_trace.c ../lib/${EDGE_SOURCES_DIR_NAME}/common/read_file.c)

include_directories (${CMAKE_CURRENT_LIST_DIR}/include)

add_executable (pt-example ${SOURCES})

add_definitions(-DMBED_CONF_MBED_TRACE_ENABLE=1)

target_include_directories (pt-example PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_include_directories (pt-example PUBLIC ${ROOT_HOME}/include)

target_link_libraries (pt-example device-interface pthread examples-common-2 pt-client-2)
