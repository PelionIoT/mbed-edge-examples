file (GLOB SOURCES ./*.c ../lib/${EDGE_SOURCES_DIR_NAME}/common/edge_trace.c ../lib/${EDGE_SOURCES_DIR_NAME}/common/read_file.c)

add_library (device-interface ${SOURCES})

add_definitions(-DMBED_CONF_MBED_TRACE_ENABLE=1)

target_include_directories (device-interface PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_include_directories (device-interface PUBLIC ${ROOT_HOME}/device-interface)
