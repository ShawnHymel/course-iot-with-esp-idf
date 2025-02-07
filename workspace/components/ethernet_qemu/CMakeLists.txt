set(srcs)
set(include_dirs "include")

if(CONFIG_ETHERNET_QEMU_CONNECT)
    list(APPEND srcs
        "ethernet_qemu.c")
endif()

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "${include_dirs}"
                       PRIV_REQUIRES esp_eth esp_netif)
