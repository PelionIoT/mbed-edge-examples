file (GLOB SOURCES ./*.c ../lib/${EDGE_SOURCES_DIR_NAME}/common/edge_trace.c)

include_directories (${CMAKE_CURRENT_LIST_DIR}/include)

add_executable (mqttpt-example ${SOURCES})

add_definitions(-DMBED_CONF_MBED_TRACE_ENABLE=1)

target_include_directories (mqttpt-example PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${PROJECT_BINARY_DIR}/lib/${EDGE_SOURCES_DIR_NAME}/lib/jansson/include)
target_include_directories (mqttpt-example PUBLIC ${ROOT_HOME}/include)

target_link_libraries (mqttpt-example pthread examples-common-2 pt-client-2 mosquitto)

