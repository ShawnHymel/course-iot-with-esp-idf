# Specify a minimum CMake version
cmake_minimum_required(VERSION 3.22.0)

# Name the project
project(
    hello_world 
    VERSION 1.0
    DESCRIPTION "The classic"
    LANGUAGES C
)

# Create a static library target named "my_lib"
add_library(
    my_lib 
    STATIC 
    src/my_lib.c
)

# Set the include directories for the library. PUBLIC adds the directory
# to the search path for any targets that link to this library.
target_include_directories(
    my_lib 
    PUBLIC   
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Create an executable target with the same name as the project name
add_executable(
    ${PROJECT_NAME} 
    src/main.c
)

# Link the library to the executable. PRIVATE means that the library is not
# exposed to targets that depend on this target.
target_link_libraries(
    ${PROJECT_NAME} 
    PRIVATE 
    my_lib
)
