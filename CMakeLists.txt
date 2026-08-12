# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# (Not part of the boilerplate)
# This example uses an extra component for common functions such as Wi-Fi and Ethernet connection.
# set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/common_components/protocol_examples_common)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Determine project name based on cluster mode from sdkconfig.
# Prefer the SDKCONFIG the build was actually told to use (idf.py -D SDKCONFIG=...)
# so per-variant build dirs (build_master/build_slave) get correctly-named
# binaries; otherwise fall back to the default top-level sdkconfig.
if(DEFINED SDKCONFIG AND NOT "${SDKCONFIG}" STREQUAL "")
    set(SDKCONFIG_FILE "${SDKCONFIG}")
else()
    set(SDKCONFIG_FILE "${CMAKE_SOURCE_DIR}/sdkconfig")
endif()
if(EXISTS ${SDKCONFIG_FILE})
    file(READ ${SDKCONFIG_FILE} SDKCONFIG_CONTENTS)
    if(SDKCONFIG_CONTENTS MATCHES "CONFIG_CLUSTER_MODE_MASTER=y")
        set(PROJECT_NAME "zombie-os-master")
    elseif(SDKCONFIG_CONTENTS MATCHES "CONFIG_CLUSTER_MODE_SLAVE=y")
        set(PROJECT_NAME "zombie-os-slave")
    else()
        set(PROJECT_NAME "zombie-os")
    endif()
else()
    set(PROJECT_NAME "zombie-os")
endif()

project(${PROJECT_NAME})

