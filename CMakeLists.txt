# Copyright (C) 2025 Dmitry Samersoff (dms@samersoff.net)
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#        http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.16)

message(Target = ${TARGET})
message(Compiler = ${CMAKE_C_COMPILER})

# Linux
if ("${TARGET}" STREQUAL "linux")
    add_compile_options(-g)
    include(main-linux/CMakeLists.txt)
endif()

# Internal testing
if ("${TARGET}" STREQUAL "moc")
    add_definitions(-D_PDU_TEST)
    add_compile_options(-g -Wall)

    include(main-moc/CMakeLists.txt)
endif()

# esp-idf by default
if ("${TARGET}" STREQUAL "")

    set(EXTRA_COMPONENT_DIRS ./components/ssd1306 ./shared)
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)

else()

    function(idf_component_register)
        # empty function to be able to share
        # CMakeList.txt file wish esp-idf
    endfunction()

    add_definitions(-DHAVE_SYSLOG)

    include(shared/CMakeLists.txt)
    include_directories("./shared")

    add_executable(s3smsf ${main_sources} ${shared_sources})

endif()

project(s3smsf)
