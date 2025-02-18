# Set the minimum required version of CMake for a project
cmake_minimum_required(VERSION 3.16)

# Include ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Add external components to the project
list(APPEND EXTRA_COMPONENT_DIRS ../../components)

# Set the project name
project(app)

# Add CA certificate to the project
target_add_binary_data(app.elf "certs/ca.crt" TEXT)
