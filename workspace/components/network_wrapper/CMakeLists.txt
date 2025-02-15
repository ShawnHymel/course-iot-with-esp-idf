# Set source files and include directories
set(srcs "network_wrapper.c")
set(include_dirs "include")

# Register the component
idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "${include_dirs}"
                       PRIV_REQUIRES esp_event ethernet_qemu wifi_sta)
