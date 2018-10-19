set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)

# Disable the firmware update, not needed when building examples
set (FIRMWARE_UPDATE OFF CACHE BOOL "")
# Set factory mode so that the edge core configuration can be skipped
set (FACTORY_MODE ON CACHE BOOL "")

add_subdirectory (${EDGE_SOURCES_DIR_NAME})
