project ("edge-pt-examples" C)
cmake_minimum_required (VERSION 3.5)

SET (EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)

set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set (ROOT_HOME ${CMAKE_CURRENT_LIST_DIR})
set (EDGE_SOURCES_DIR_NAME "mbed-edge")
find_package (Threads REQUIRED)

# Utility cmake functions
# Use the ones in edge to avoid duplication
include (lib/${EDGE_SOURCES_DIR_NAME}/cmake/common.cmake)

include(git_details.cmake)

# Edge basic configuration
include (cmake/examples_configure.cmake)

# Edge include directories
include (cmake/examples_include_directories.cmake)


add_subdirectory (examples-common)
add_subdirectory (examples-common-2)
add_subdirectory (device-interface)
add_subdirectory (byte-order)
add_subdirectory (lib/${EDGE_SOURCES_DIR_NAME}/nano-stack)
add_subdirectory (lib/${EDGE_SOURCES_DIR_NAME}/mbed-trace-edge)

add_subdirectory (lib/${EDGE_SOURCES_DIR_NAME}/lib/jansson)
add_library (jsonrpc STATIC lib/${EDGE_SOURCES_DIR_NAME}/lib/jsonrpc/jsonrpc.c)
target_link_libraries(jsonrpc jansson)

add_subdirectory (lib/${EDGE_SOURCES_DIR_NAME}/common)
add_subdirectory (lib/${EDGE_SOURCES_DIR_NAME}/edge-rpc)
add_subdirectory (lib/${EDGE_SOURCES_DIR_NAME}/pt-client)
add_subdirectory (lib/${EDGE_SOURCES_DIR_NAME}/pt-client-2)
add_subdirectory (lib/${EDGE_SOURCES_DIR_NAME}/lib/libevent)
add_subdirectory (lib/${EDGE_SOURCES_DIR_NAME}/lib/libwebsockets)
add_subdirectory (pt-example)

# Conditionally build ble-pt if glib-2.0 is available
unset(GLIB2_LIBRARIES CACHE)
find_library(GLIB2_LIBRARIES glib-2.0)
if (SKIP_LIB_CHECK OR GLIB2_LIBRARIES)
  add_subdirectory (blept-example)
else ()
  message("Not building the bluetooth example, glib-2.0 library was not found!")
endif ()

# Conditionally build mqtt-pt if mosquitto is available
unset(MOSQUITTO_LIB CACHE)
find_library(MOSQUITTO_LIB mosquitto)
if (SKIP_LIB_CHECK OR MOSQUITTO_LIB)
  add_subdirectory (mqttpt-example)
else ()
  message("Not building the mqtt protocol translator since mosquitto library was not found!")
endif ()

# Add doc target for building documentation with Doxygen
find_package (Doxygen)
option (BUILD_DOCUMENTATION "Create and install the HTML based API documentation (requires Doxygen)" ${DOXYGEN_FOUND})
if (BUILD_DOCUMENTATION)
    if (NOT DOXYGEN_FOUND)
        message (FATAL_ERROR "Doxygen is needed to build the documentation.")
    endif ()

    set (doxyfile_in ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set (doxyfile ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    configure_file (${doxyfile_in} ${doxyfile} @ONLY)

    add_custom_target (edge-examples-doc
        COMMAND ${DOXYGEN_EXECUTABLE} ${doxyfile}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM)
endif ()
