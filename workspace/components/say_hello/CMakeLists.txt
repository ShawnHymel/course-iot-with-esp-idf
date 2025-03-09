# Set source files and include directories
set(srcs)
set(include_dirs "include")

# Conditionally add source files
if(CONFIG_SAY_HELLO)
    list(APPEND srcs
        "say_hello.c")
endif()

# Register the component
idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "${include_dirs}")
