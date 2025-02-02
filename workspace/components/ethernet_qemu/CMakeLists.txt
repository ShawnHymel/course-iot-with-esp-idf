idf_component_register(SRCS "ethernet_qemu.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_eth esp_netif
                       )