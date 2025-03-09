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

set(sources "smsf-ata.c" "smsf-pdu.c" "smsf-util.c" "smsf-logging.c" "smsf-flow.c")
idf_component_register(SRCS ${sources}
                       INCLUDE_DIRS ".")

set(shared_sources "")
foreach(src ${sources})
   list(APPEND shared_sources "${CMAKE_CURRENT_LIST_DIR}/${src}")
endforeach()
