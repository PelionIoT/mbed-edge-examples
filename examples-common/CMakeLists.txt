file (GLOB SOURCES ./*.c ../lib/${EDGE_SOURCES_DIR_NAME}/common/edge_trace.c ../lib/${EDGE_SOURCES_DIR_NAME}/common/read_file.c)

include_directories (${CMAKE_CURRENT_LIST_DIR}/include)

add_library (examples-common ${SOURCES})

add_definitions(-DMBED_CONF_MBED_TRACE_ENABLE=1)

target_include_directories (examples-common PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_include_directories (examples-common PUBLIC ${ROOT_HOME}/examples-common)

target_link_libraries (examples-common byte-order device-interface pthread pt-client nanostack)
