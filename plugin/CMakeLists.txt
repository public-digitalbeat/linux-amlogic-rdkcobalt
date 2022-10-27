# Copyright 2020 Comcast Cable Communications Management, LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.3)

project(Cobalt)

find_package(WPEFramework)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(PLUGIN_NAME Cobalt)
set(MODULE_NAME ${NAMESPACE}${PLUGIN_NAME})

find_package(${NAMESPACE}Plugins REQUIRED)

 find_package(JsonGenerator REQUIRED)
 JsonGenerator(DOCS
   INPUT  ${CMAKE_CURRENT_SOURCE_DIR}/CobaltPlugin.json
   IFDIR  ${CMAKE_CURRENT_SOURCE_DIR}/interfaces/json/
   OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/doc/
 )
# JsonGenerator(CODE STUBS
#   INPUT  ${CMAKE_CURRENT_SOURCE_DIR}/CobaltPlugin.json
#   IFDIR  ${CMAKE_CURRENT_SOURCE_DIR}/interfaces/json/
#   OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/interfaces/json/
# )

option(PLUGIN_COBALT_HTMLAPP "Include htmlapp in seperate plugin." OFF)

add_library(${MODULE_NAME} SHARED
    Cobalt.cpp
    CobaltImplementation.cpp
    CobaltJsonRpc.cpp
    Module.cpp)

target_link_libraries(${MODULE_NAME}
    PRIVATE
        ${NAMESPACE}Plugins::${NAMESPACE}Plugins
        -lcobalt
        )

# Library installation section
string(TOLOWER ${NAMESPACE} STORAGENAME)
install(TARGETS ${MODULE_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/${STORAGENAME}/plugins)

set(PLUGIN_COBALT_AUTOSTART false CACHE STRING "Automatically start Cobalt plugin")
set(PLUGIN_COBALT_OUTOFPROCESS true CACHE STRING "Controls if the plugin should run in its own process")

set(PLUGIN_HTMLAPP_AUTOSTART false CACHE STRING "Automatically start HtmlApp plugin")
set(PLUGIN_HTMLAPP_OUTOFPROCESS true CACHE STRING "Controls if the plugin should run in its own process")

write_config(${PLUGIN_NAME})

if(PLUGIN_COBALT_HTMLAPP)
    write_config( CobaltHtmlApp )
endif()
