file (GLOB SOURCES ./*.c ../lib/${EDGE_SOURCES_DIR_NAME}/common/edge_trace.c ../lib/${EDGE_SOURCES_DIR_NAME}/common/read_file.c)

include_directories (${CMAKE_CURRENT_LIST_DIR}/include)

include(FindPkgConfig)
FIND_PACKAGE(PkgConfig)
PKG_CHECK_MODULES(GLIB2 REQUIRED glib-2.0>=2.36.0)
PKG_CHECK_MODULES(GIO2  REQUIRED gio-2.0>=2.36.0)

add_definitions(-DMBED_CONF_MBED_TRACE_ENABLE=1)

add_executable(blept-example ${SOURCES})

target_include_directories (blept-example PUBLIC ${ROOT_HOME}/include)
target_include_directories (blept-example PUBLIC ${CMAKE_CURRENT_LIST_DIR}, ${GLIB2_INCLUDE_DIRS})

target_link_libraries(blept-example pthread device-interface examples-common-2 pt-client-2 byte-order ${GIO2_LIBRARIES} ${GLIB2_LIBRARIES} -lpthread -lm )

target_compile_options(blept-example PUBLIC ${GLIB2_OTHER_CFLAGS} )
