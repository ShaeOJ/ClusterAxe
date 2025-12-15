# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# (Not part of the boilerplate)
# This example uses an extra component for common functions such as Wi-Fi and Ethernet connection.
# set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/common_components/protocol_examples_common)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Determine project name based on cluster mode from sdkconfig
# Read sdkconfig to check if we're building master or slave
set(SDKCONFIG_FILE "${CMAKE_SOURCE_DIR}/sdkconfig")
if(EXISTS ${SDKCONFIG_FILE})
    file(READ ${SDKCONFIG_FILE} SDKCONFIG_CONTENTS)
    if(SDKCONFIG_CONTENTS MATCHES "CONFIG_CLUSTER_MODE_MASTER=y")
        set(PROJECT_NAME "clusteraxe-master")
    elseif(SDKCONFIG_CONTENTS MATCHES "CONFIG_CLUSTER_MODE_SLAVE=y")
        set(PROJECT_NAME "clusteraxe-slave")
    else()
        set(PROJECT_NAME "clusteraxe")
    endif()
else()
    set(PROJECT_NAME "clusteraxe")
endif()

project(${PROJECT_NAME})

