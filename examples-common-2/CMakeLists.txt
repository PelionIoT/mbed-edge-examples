file (GLOB SOURCES ./*.c ../lib/${EDGE_SOURCES_DIR_NAME}/common/edge_trace.c ../lib/${EDGE_SOURCES_DIR_NAME}/common/read_file.c)

include_directories (${CMAKE_CURRENT_LIST_DIR}/include)

add_library (examples-common-2 ${SOURCES})

add_definitions(-DMBED_CONF_MBED_TRACE_ENABLE=1)

target_include_directories (examples-common-2 PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_include_directories (examples-common-2 PUBLIC ${ROOT_HOME}/examples-common-2)

target_link_libraries (examples-common-2 device-interface byte-order pthread pt-client-2 nanostack)
