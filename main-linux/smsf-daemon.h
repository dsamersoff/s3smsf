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

#ifndef _SMSF_DAEMON_H
#define _SMSF_DAEMON_H

 #include <signal.h>

#ifndef PID_PATH
  #define PID_PATH "/var/run"
#endif

typedef void sigfunc_t(int);   /* for signal handlers */

void daemonize(const char *prog_name);
int kill_running(const char *progname);

#endif
