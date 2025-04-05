/*
 * Copyright (C) 2025 Dmitry Samersoff (dms@samersoff.net)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _SMSF_FLOW_H
#define _SMSF_FLOW_H

typedef void (notify_func_t)(char *format, ... );

/**
 * @brief Process special types of images
 *
 * @param device
 * @param text - command
 * @return int - 1 if command processed, 0 if command is not recognized
 */
int process_command_message(int device, const char *text);

/**
 * @brief Pre-requsites for main flow loop
 *
 * @param device
 * @param disp_func
 * @return int
 */
int flow_setup(int device, notify_func_t *notify_func, const char *da_override);


/**
 * @brief Main flow loop
 *
 * @param device
 * @param disp_func
 * @return int
 */

int flow(int device, notify_func_t *notify_func);

#endif
