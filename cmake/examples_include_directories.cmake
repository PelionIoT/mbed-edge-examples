include_directories (include)
include_directories (config)
include_directories (lib/${EDGE_SOURCES_DIR_NAME}/include)
include_directories (lib/${EDGE_SOURCES_DIR_NAME}/common)

SET (MBED_EDGE_DEPENDENCY_SOURCES "${ROOT_HOME}/lib/${EDGE_SOURCES_DIR_NAME}/lib")
include_directories (${MBED_EDGE_DEPENDENCY_SOURCES}/mbed-cloud-client/${MBED_CLOUD_CLIENT_DEPENDENCY_SOURCES}/nanostack-libservice/)
include_directories (${MBED_EDGE_DEPENDENCY_SOURCES}/mbed-cloud-client/nanostack-libservice/mbed-client-libservice)
include_directories (${MBED_EDGE_DEPENDENCY_SOURCES}/mbed-cloud-client/mbed-trace)

# Generated headers for jansson, include generated headers too
include_directories (${MBED_EDGE_DEPENDENCY_SOURCES}/jansson/jansson/src)
include_directories (${CMAKE_CURRENT_BINARY_DIR}/lib/${EDGE_SOURCES_DIR_NAME}/lib/jansson/jansson/include/)

# Libevent include, include generated headers too
include_directories (lib/${EDGE_SOURCES_DIR_NAME}/lib/libevent/libevent/include)
include_directories (${CMAKE_CURRENT_BINARY_DIR}/lib/${EDGE_SOURCES_DIR_NAME}/lib/libevent/libevent/include)

include_directories (${MBED_EDGE_DEPENDENCY_SOURCES}/libwebsockets/libwebsockets/lib)
include_directories (${CMAKE_BINARY_DIR}/lib/${EDGE_SOURCES_DIR_NAME}/lib/libwebsockets/libwebsockets)
include_directories (${CMAKE_BINARY_DIR}/lib/${EDGE_SOURCES_DIR_NAME}/lib/libwebsockets/libwebsockets/include)

