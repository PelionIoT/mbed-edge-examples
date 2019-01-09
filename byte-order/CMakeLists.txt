file (GLOB SOURCES ./*.c)

add_library (byte-order ${SOURCES})

target_include_directories (byte-order PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_include_directories (byte-order PUBLIC ${ROOT_HOME}/byte-order)

