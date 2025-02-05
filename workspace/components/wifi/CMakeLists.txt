set(srcs)
set(include_dirs "include")

if(CONFIG_CONNECT_WIFI)
    list(APPEND srcs
        "wifi.c")
endif()

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "${include_dirs}"
                       PRIV_REQUIRES esp_wifi esp_netif)
